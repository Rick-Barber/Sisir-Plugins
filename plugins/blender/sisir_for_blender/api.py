"""Scripting: what 3ds Max's `sisir` interface offers (SisirInterface.cpp), for Blender's Python.

    import sisir_for_blender.api as sisir          (bl_ext.<repo>.sisir_for_blender.api as an extension)
    sisir.import_file("D:/heads/anna.sir", options="cards=0;unit=cm", no_dialog=True)
    sisir.export_abc("D:/out/anna.abc", options="units=cm;color=1", no_dialog=True)

Without `no_dialog` both open the same dialogs as the Sisir menu and return at once.
"""

import ctypes
import os

import bpy

from . import bridge as _bridge
from . import export_abc as _abc
from . import export_ply as _ply
from . import hair as _hair
from .builder import Builder, legal_name
from .options import ImportOptions
from .sirbake import Bake, BakeError


class SisirError(Exception):
    pass


def log(message):
    print("Sisir: " + message)


def log_warning(message):
    print("Sisir warning: " + message)


def _documents():
    try:
        from ctypes import wintypes

        class GUID(ctypes.Structure):
            _fields_ = [("a", wintypes.DWORD), ("b", wintypes.WORD), ("c", wintypes.WORD), ("d", ctypes.c_ubyte * 8)]
        documents = GUID(0xFDD39AD0, 0x238F, 0x46AF, (ctypes.c_ubyte * 8)(0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7))
        out = ctypes.c_wchar_p()
        if ctypes.windll.shell32.SHGetKnownFolderPath(ctypes.byref(documents), 0, None, ctypes.byref(out)) == 0:
            path = out.value
            ctypes.windll.ole32.CoTaskMemFree(out)
            return path
    except (AttributeError, OSError):
        pass
    return os.path.join(os.path.expanduser("~"), "Documents")


def default_texture_folder(scene_name):
    """<.blend folder>/sisir/<scene>, or Documents/Blender/sisir/<scene> before the file is saved
    (Max's sceneassets/images/sisir/<scene>, Maya's sourceimages/sisir/<scene>)."""
    base = os.path.dirname(bpy.data.filepath) if bpy.data.filepath else os.path.join(_documents(), "Blender")
    return os.path.join(base, "sisir", legal_name(scene_name))


def plugin_path():
    """Which copy of the add-on is loaded."""
    return os.path.dirname(os.path.abspath(__file__))


def hair_stats(obj):
    obj = bpy.data.objects[obj] if isinstance(obj, str) else obj
    return _hair.hair_stats(obj) if _hair.is_hair(obj) else ""


def build(context, bake_file, assets_dir, options, source_file, progress=None):
    """Builds a bake into the scene; returns (builder, ok). A failed or cancelled build is undone."""
    try:
        bake = Bake(bake_file)
    except BakeError as e:
        raise SisirError(str(e))
    builder = Builder(context, bake, options, assets_dir, os.path.normpath(source_file))
    try:
        ok = builder.build(progress)
    except Exception:
        builder.discard()
        raise
    if not ok:
        builder.discard()
    return builder, ok


def import_file(path, options="", no_dialog=False, sisir=""):
    """Imports a .sir; returns the root object's name (empty when the dialog runs, or on failure)."""
    path = os.path.abspath(path)
    if not no_dialog:
        bpy.ops.sisir.import_scene("INVOKE_DEFAULT", filepath=path, options=options, sisir=sisir)
        return ""
    if not os.path.isfile(path):
        raise SisirError("file not found: " + path)
    opts = ImportOptions.from_string(options, ImportOptions.load_remembered())
    try:
        bridge_exe, sisir_exe = _bridge.find_bridge(sisir or opts.sisirExecutable)
    except _bridge.BridgeError as e:
        raise SisirError(str(e) + ("" if "update" in str(e) else ". Install Sisir, run it once, or pass its location with sisir="))
    scene_name = os.path.splitext(os.path.basename(path))[0]
    assets = opts.textureFolder or default_texture_folder(scene_name)
    last = [-1]

    def report(fraction, label):
        pct = int(fraction * 100)
        if fraction >= 0 and pct // 10 != last[0] // 10:
            log("%d%% %s" % (pct, label))
            last[0] = pct

    result = error = None
    bake_share = 0.6
    report(0.0, "Generating hair and cards in Sisir...")
    for line in _bridge.run(bridge_exe, ["bake", path, "--assets", assets, "--sisir", sisir_exe]):
        if line.startswith("PROGRESS "):
            parts = line.split(" ", 2)
            report(float(parts[1]) * bake_share, "Sisir: " + (parts[2] if len(parts) > 2 else ""))
        elif line.startswith("RESULT\t"):
            result = line.split("\t")
        elif line.startswith("ERROR\t"):
            error = line.split("\t", 1)[1]
        elif line == "CANCEL":
            error = "Import cancelled"
    if result is None:
        raise SisirError(error or "Sisir could not prepare the scene")
    builder, ok = build(bpy.context, result[1], result[2], opts, path,
                        lambda f, label: report(bake_share + (1 - bake_share) * f, label) if f >= 0 else None)
    for w in builder.report.warnings:
        log_warning(w)
    log(builder.summary())
    bpy.ops.ed.undo_push(message="Import Sisir Scene")
    return builder.report.root.name if builder.report.root else ""


def export_abc_count():
    """How many hair objects the selection would export."""
    return len(_abc.targets(bpy.context))


def export_abc(path, options="", no_dialog=False):
    """Exports the selected Sisir hair; returns the written file (empty when the dialog runs)."""
    if not no_dialog:
        bpy.ops.sisir.export_hair_abc("INVOKE_DEFAULT", filepath=path, options=options)
        return ""
    objects = _abc.targets(bpy.context)
    if not objects:
        raise SisirError(_abc.SELECT_HAIR)
    bridge_exe, _ = _bridge.find_bridge()
    dump = _abc.dump_path()
    _abc.write_dump(objects, dump)
    try:
        lines = _bridge.run(bridge_exe, ["export-abc", dump, os.path.abspath(path), "--options", options, "--no-dialog"])
    finally:
        try:
            os.remove(dump)
        except OSError:
            pass
    for line in lines:
        if line.startswith("WARNING\t"):
            log_warning(line.split("\t", 1)[1])
        elif line.startswith("ERROR\t"):
            raise SisirError(line.split("\t", 1)[1])
        elif line.startswith("RESULT\t"):
            _, file, sets, strands = line.split("\t")
            log("exported %s hair set%s (%s strands) to %s" % (sets, "" if sets == "1" else "s", strands, file))
            return file
    return ""


def export_guides_ply(path, unit=None):
    """Exports the selected curves as Sisir guides in `unit` (mm, cm or m; default: the unit Export
    Guides (PLY) asked for last, metres before that); returns the message it shows."""
    message, _, _ = _ply.export(bpy.context, path, unit or _ply.remembered_unit())
    log(message)
    return message
