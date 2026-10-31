"""Sisir for Blender: import Sisir scenes (mesh, guides, hair, cards), export hair as Sisir's ABC and
curves as Sisir guides -- the Blender side of the plug-in Sisir ships for Maya and 3ds Max.

The Qt half (the import dialog, the bake, the Export Hair (ABC) dialog and writer) is SisirBridge.exe,
next to Sisir.exe; see bridge.py. Installed and updated from Sisir's Plugins > Blender menu.
"""

bl_info = {
    "name": "Sisir for Blender",
    "author": "Sisir",
    "version": (1, 3, 0),
    "blender": (4, 2, 0),
    "location": "Sisir menu, File > Import > Sisir Scene (.sir)",
    "description": "Import Sisir scenes: mesh, guides, hair rendered in Cycles and Eevee, cards",
    "category": "Import-Export",
}

import os
import time

import bpy
from bpy.app.handlers import persistent
from bpy.props import StringProperty
from bpy_extras.io_utils import ExportHelper, ImportHelper

from . import api
from . import bridge as _bridge
from . import export_abc as _abc
from . import export_ply as _ply
from . import hair as _hair
from . import tooltips
from .builder import Builder
from .options import ImportOptions
from .sirbake import Bake, BakeError

_TICK = 0.05          # seconds between looks at the bridge
_BUILD_SLICE = 0.04   # seconds of building per tick, so Blender keeps painting


class _BridgeModal:
    """A bridge command run behind Blender's event loop, with Blender's windows locked like under a
    modal dialog. Subclasses handle the bridge's lines (on_line) and its exit (on_exit), each
    returning None to go on or the operator's result."""

    _session = None
    _lock = None
    _timer = None

    def begin(self, context, bridge_exe, args):
        self._lock = _bridge.ModalLock()
        try:
            self._session = _bridge.Session(bridge_exe, list(args) + ["--owner", str(self._lock.owner or 0)])
        except _bridge.BridgeError:
            self._lock.release()
            raise
        wm = context.window_manager
        self._timer = wm.event_timer_add(_TICK, window=context.window)
        wm.modal_handler_add(self)
        return {"RUNNING_MODAL"}

    def unlock(self):
        """Blender's windows on again -- before the bridge is told to close its dialog: Windows hands
        the focus back to the dialog's owner only if that is enabled when the dialog hides."""
        if self._lock is not None:
            self._lock.release()
            self._lock = None

    def end(self, context):
        if self._timer is not None:
            context.window_manager.event_timer_remove(self._timer)
            self._timer = None
        if self._session is not None:
            self._session.close()
            self._session = None
        if self._lock is not None:
            self._lock.release()
            self._lock = None

    def modal(self, context, event):
        if event.type != "TIMER":
            return {"RUNNING_MODAL"}  # Blender is locked behind the dialog
        try:
            result = self.tick(context)
            if result is None:
                for line in self._session.lines():
                    result = self.on_exit(context) if line is None else self.on_line(context, line)
                    if result is not None:
                        break
        except _bridge.BridgeError as e:
            self.report({"ERROR"}, str(e))
            result = {"CANCELLED"}
        except Exception as e:
            self.report({"ERROR"}, "Sisir: %s" % e)
            self.fail(context, str(e))
            result = {"CANCELLED"}
        if result is None:
            return {"RUNNING_MODAL"}
        self.end(context)
        return result

    def cancel(self, context):
        # Blender stopping the operator (a file load, the window closing): the dialog goes too.
        self.fail(context, "")
        if self._session is not None:
            self._session.close(kill=True)
            self._session = None
        self.end(context)

    def tick(self, context):
        return None

    def fail(self, context, message):
        pass


def _find_bridge_or_locate(op, explicit, then_import=""):
    try:
        return _bridge.find_bridge(explicit)
    except _bridge.BridgeError as e:
        if "not found" in str(e) and then_import:
            bpy.ops.sisir.locate_sisir("INVOKE_DEFAULT", import_path=then_import)
        else:
            op.report({"ERROR"}, str(e))
        return None


class SISIR_OT_import_scene(_BridgeModal, bpy.types.Operator, ImportHelper):
    bl_idname = "sisir.import_scene"
    bl_label = "Import Sisir Scene"
    bl_description = tooltips.text("blender.menu.importScene")
    bl_options = {"UNDO"}

    filename_ext = ".sir"
    # Not remembered: a path here means "this file" (a drop, a script), never last time's.
    filepath: StringProperty(subtype="FILE_PATH", options={"SKIP_SAVE"})
    filter_glob: StringProperty(default="*.sir", options={"HIDDEN"})
    options: StringProperty(default="", options={"HIDDEN", "SKIP_SAVE"})
    sisir: StringProperty(default="", options={"HIDDEN", "SKIP_SAVE"})

    _builder = None
    _steps = None
    _cancel_requested = False
    _last_progress = 0.0

    def invoke(self, context, event):
        if self.filepath and os.path.isfile(self.filepath):  # dropped on Blender, or scripted
            return self.execute(context)
        return ImportHelper.invoke(self, context, event)

    def execute(self, context):
        path = os.path.abspath(bpy.path.abspath(self.filepath))
        if not os.path.isfile(path):
            self.report({"ERROR"}, "file not found: " + path)
            return {"CANCELLED"}
        found = _find_bridge_or_locate(self, self.sisir, then_import=path)
        if found is None:
            return {"CANCELLED"}
        bridge_exe, sisir_exe = found
        scene_name = os.path.splitext(os.path.basename(path))[0]
        self._path = path
        args = ["import", path, "--assets", api.default_texture_folder(scene_name), "--sisir", sisir_exe]
        if self.options:
            args += ["--options", self.options]
        try:
            return self.begin(context, bridge_exe, args)
        except _bridge.BridgeError as e:
            self.report({"ERROR"}, str(e))
            return {"CANCELLED"}

    def on_line(self, context, line):
        if self._steps is not None:
            if line == "CANCEL":
                self._cancel_requested = True
            return None
        if line.startswith("IMPORT\t"):
            _, bake_file, assets, options = (line.split("\t") + ["", "", ""])[:4]
            try:
                bake = Bake(bake_file)
            except BakeError as e:
                self.unlock()
                self._session.send("FAIL\t" + str(e))
                self.report({"ERROR"}, str(e))
                return {"CANCELLED"}
            opts = ImportOptions.from_string(options)
            self._builder = Builder(context, bake, opts, assets, os.path.normpath(self._path))
            self._steps = self._builder.steps()
            return None
        if line.startswith("ERROR\t"):
            self.report({"ERROR"}, line.split("\t", 1)[1])
            return {"CANCELLED"}
        if line == "CANCEL":
            return {"CANCELLED"}
        return None  # the bake's PROGRESS: the dialog shows it

    def on_exit(self, context):
        if self._steps is not None:
            return None  # the dialog is gone, the build goes on
        return {"CANCELLED"}

    def tick(self, context):
        if self._steps is None:
            return None
        if self._cancel_requested:
            self._builder.discard()
            self.unlock()
            self._session.send("CANCELLED")
            self.report({"INFO"}, "Import cancelled")
            return {"CANCELLED"}
        deadline = time.perf_counter() + _BUILD_SLICE
        while time.perf_counter() < deadline:
            try:
                fraction, label = next(self._steps)
            except StopIteration:
                self.unlock()
                self._session.send("DONE")
                for w in self._builder.report.warnings:
                    api.log_warning(w)
                    self.report({"WARNING"}, w)
                summary = self._builder.summary()
                api.log(summary)
                self.report({"INFO"}, summary)
                return {"FINISHED"}
            if fraction >= 0.0 or label:
                self._session.send("PROGRESS %.3f %s" % (max(fraction, self._last_progress), label))
                self._last_progress = max(fraction, self._last_progress)
            # Lines in between: a cancel stops the build at the next object.
            for line in self._session.lines():
                if line == "CANCEL":
                    self._cancel_requested = True
            if self._cancel_requested:
                break
        return None

    def fail(self, context, message):
        if self._builder is not None:
            self._builder.discard()
            self._builder = None
        if self._session is not None:
            self.unlock()
            self._session.send("FAIL\t" + message if message else "CANCELLED")


class SISIR_OT_locate_sisir(bpy.types.Operator, ImportHelper):
    bl_idname = "sisir.locate_sisir"
    bl_label = "Locate Sisir.exe"
    bl_description = tooltips.text("blender.menu.locateSisir")
    filename_ext = ".exe"
    filter_glob: StringProperty(default="Sisir.exe", options={"HIDDEN"})
    import_path: StringProperty(default="", options={"HIDDEN", "SKIP_SAVE"})

    def invoke(self, context, event):
        self.report({"INFO"}, "Sisir was not found on this computer. Show where Sisir.exe is.")
        self.filepath = "Sisir.exe"
        return ImportHelper.invoke(self, context, event)

    def execute(self, context):
        exe = bpy.path.abspath(self.filepath)
        if not os.path.isfile(exe) or os.path.basename(exe).lower() != "sisir.exe":
            self.report({"ERROR"}, "Pick Sisir.exe")
            return {"CANCELLED"}
        if not os.path.isfile(os.path.join(os.path.dirname(exe), _bridge.BRIDGE_EXE)):
            self.report({"ERROR"}, "this Sisir is too old to import into Blender: update Sisir")
            return {"CANCELLED"}
        _bridge.remember_sisir(exe)
        if self.import_path:
            bpy.ops.sisir.import_scene("INVOKE_DEFAULT", filepath=self.import_path)
        return {"FINISHED"}


class SISIR_OT_export_hair_abc(_BridgeModal, bpy.types.Operator, ExportHelper):
    bl_idname = "sisir.export_hair_abc"
    bl_label = "Export Hair (ABC)"
    bl_description = tooltips.text("blender.menu.exportHairAbc")

    filename_ext = ".abc"
    filepath: StringProperty(subtype="FILE_PATH", options={"SKIP_SAVE"})
    filter_glob: StringProperty(default="*.abc", options={"HIDDEN"})
    options: StringProperty(default="", options={"HIDDEN", "SKIP_SAVE"})

    _dump = ""

    def invoke(self, context, event):
        # Sisir's order: check the selection, where to, then the export dialog.
        if not _abc.targets(context):
            self.report({"ERROR"}, _abc.SELECT_HAIR)
            return {"CANCELLED"}
        if self.filepath:
            return self.execute(context)
        return ExportHelper.invoke(self, context, event)

    def execute(self, context):
        objects = _abc.targets(context)
        if not objects:
            self.report({"ERROR"}, _abc.SELECT_HAIR)
            return {"CANCELLED"}
        try:
            bridge_exe, _ = _bridge.find_bridge()
        except _bridge.BridgeError as e:
            self.report({"ERROR"}, str(e))
            return {"CANCELLED"}
        self._dump = _abc.dump_path()
        _abc.write_dump(objects, self._dump)
        args = ["export-abc", self._dump, os.path.abspath(bpy.path.abspath(self.filepath))]
        if self.options:
            args += ["--options", self.options]
        try:
            return self.begin(context, bridge_exe, args)
        except _bridge.BridgeError as e:
            self.report({"ERROR"}, str(e))
            return {"CANCELLED"}

    def on_line(self, context, line):
        if line.startswith("WARNING\t"):
            api.log_warning(line.split("\t", 1)[1])
            self.report({"WARNING"}, line.split("\t", 1)[1])
        elif line.startswith("ERROR\t"):
            self.report({"ERROR"}, line.split("\t", 1)[1])
            return {"CANCELLED"}
        elif line.startswith("RESULT\t"):
            _, file, sets, strands = line.split("\t")
            message = "exported %s hair set%s (%s strands) to %s" % (sets, "" if sets == "1" else "s", strands, file)
            api.log(message)
            self.report({"INFO"}, message)
            return {"FINISHED"}
        elif line == "CANCEL":
            return {"CANCELLED"}
        return None

    def on_exit(self, context):
        return {"CANCELLED"}

    def end(self, context):
        super().end(context)
        if self._dump:
            try:
                os.remove(self._dump)
            except OSError:
                pass
            self._dump = ""


class SISIR_OT_export_guides_ply(_BridgeModal, bpy.types.Operator, ExportHelper):
    bl_idname = "sisir.export_guides_ply"
    bl_label = "Export Guides (PLY)"
    bl_description = tooltips.text("blender.menu.exportGuidesPly")

    filename_ext = ".ply"
    filepath: StringProperty(subtype="FILE_PATH", options={"SKIP_SAVE"})
    filter_glob: StringProperty(default="*.ply", options={"HIDDEN"})
    unit: StringProperty(default="", options={"HIDDEN", "SKIP_SAVE"})  # mm, cm or m: skips the popup

    def invoke(self, context, event):
        # Sisir's order: check the selection, where to, then the unit.
        if not _ply.sources(context):
            self.report({"ERROR"}, "Select at least one curve object to export")
            return {"CANCELLED"}
        if self.filepath:
            return self.execute(context)
        return ExportHelper.invoke(self, context, event)

    def execute(self, context):
        if self.unit in ("mm", "cm", "m"):
            return self.write(context, self.unit)
        try:
            bridge_exe, _ = _bridge.find_bridge()
            # The unit popup Sisir for Maya and 3ds Max show, from SisirBridge.
            return self.begin(context, bridge_exe, ["ply-units"])
        except _bridge.BridgeError:
            return self.write(context, _ply.remembered_unit())

    def write(self, context, unit):
        try:
            message, _, _ = _ply.export(context, bpy.path.abspath(self.filepath), unit)
        except (RuntimeError, OSError, ValueError) as e:
            self.report({"ERROR"}, str(e))
            return {"CANCELLED"}
        api.log(message)
        self.report({"INFO"}, message)
        return {"FINISHED"}

    def on_line(self, context, line):
        if line.startswith("RESULT\t"):
            self.unlock()
            return self.write(context, line.split("\t", 1)[1].strip())
        if line == "CANCEL":
            return {"CANCELLED"}
        if line.startswith("ERROR\t"):
            self.report({"ERROR"}, line.split("\t", 1)[1])
            return {"CANCELLED"}
        return None

    def on_exit(self, context):
        return {"CANCELLED"}


class TOPBAR_MT_sisir(bpy.types.Menu):
    bl_idname = "TOPBAR_MT_sisir"
    bl_label = "Sisir"

    def draw(self, context):
        layout = self.layout
        layout.operator_context = "INVOKE_DEFAULT"
        layout.operator(SISIR_OT_import_scene.bl_idname, text="Import Sisir Scene...")
        layout.separator()
        layout.operator(SISIR_OT_export_hair_abc.bl_idname, text="Export Hair (ABC)...")
        layout.operator(SISIR_OT_export_guides_ply.bl_idname, text="Export Guides (PLY)...")


class SISIR_FH_sir(bpy.types.FileHandler):
    bl_idname = "SISIR_FH_sir"
    bl_label = "Sisir Scene"
    bl_import_operator = "sisir.import_scene"
    bl_file_extensions = ".sir"

    @classmethod
    def poll_drop(cls, context):
        return context.area is not None and context.area.type in {"VIEW_3D", "OUTLINER"}


def _menu_topbar(self, context):
    self.layout.menu(TOPBAR_MT_sisir.bl_idname)


def _menu_import(self, context):
    self.layout.operator(SISIR_OT_import_scene.bl_idname, text="Sisir Scene (.sir)")


@persistent
def _load_pre(*_):
    # A bridge dialog never outlives the file it was importing into.
    _bridge.ModalLock.release_all()


_classes = (SISIR_OT_import_scene, SISIR_OT_locate_sisir, SISIR_OT_export_hair_abc, SISIR_OT_export_guides_ply,
            TOPBAR_MT_sisir, SISIR_FH_sir)


def register():
    _hair.register()
    for c in _classes:
        bpy.utils.register_class(c)
    bpy.types.TOPBAR_MT_editor_menus.append(_menu_topbar)
    bpy.types.TOPBAR_MT_file_import.append(_menu_import)
    bpy.app.handlers.load_pre.append(_load_pre)


def unregister():
    _bridge.ModalLock.release_all()
    if _load_pre in bpy.app.handlers.load_pre:
        bpy.app.handlers.load_pre.remove(_load_pre)
    bpy.types.TOPBAR_MT_file_import.remove(_menu_import)
    bpy.types.TOPBAR_MT_editor_menus.remove(_menu_topbar)
    for c in reversed(_classes):
        bpy.utils.unregister_class(c)
    _hair.unregister()
