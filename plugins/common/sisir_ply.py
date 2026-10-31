# -*- coding: utf-8 -*-
"""Sisir guide PLY files, as every Sisir DCC plug-in writes and reads them.

One writer for Sisir for Maya (Scripts/Maya/Maya_curves_to_PLY.py), Sisir for 3ds Max
(Scripts/3DSMax/3DSMax_to_PLY.py) and Sisir for Blender (export_ply.py), so the same guides give
the same file from any of them, byte for byte (tools/dcc_export_consistency.py checks it):

    ply
    format ascii 1.0
    comment sisir_unit <unit>                              mm, cm or m: Export Guides (PLY) asks
    comment layer <id> "<name>" <r> <g> <b> <visible>      layers by id; colours in 8-bit steps
    element vertex <n>
    property float x / y / z, uchar anchor, int layer_id, int curve_id
    end_header
    <x> <y> <z> <anchor> <layer_id> <curve_id>             Sisir's Y-up, in <unit>, %.6f

The unit is one of the three Sisir's PLY import offers, asked on export (PlyExportDialog) and
remembered per plug-in. Curves go out layer by layer in the order given; each curve's first point is
its anchor (root). A coordinate is the float32 of its value in the unit, computed by each plug-in
from its own numbers in one factor: the precision every DCC holds an imported guide in, so the
numbers agree to the bit wherever a DCC kept them exactly (Maya, Blender, and 3ds Max in metric
units; a 3ds Max scene in inches keeps them to about a micron).

Python 2.7 and 3.x, no dependencies: it runs inside Maya, 3ds Max and Blender as they ship.
"""

import io
import struct

EXPORT_UNITS = ("mm", "cm", "m")

# Metres per unit, for every label a Sisir PLY may carry: Sisir's own (mm, cm, m) and what earlier
# DCC exporters wrote (Maya's linear unit names, 3ds Max's unit names).
UNIT_METRES = {
    "mm": 0.001, "millimeter": 0.001, "millimeters": 0.001, "millimetre": 0.001, "millimetres": 0.001,
    "cm": 0.01, "centimeter": 0.01, "centimeters": 0.01, "centimetre": 0.01, "centimetres": 0.01,
    "m": 1.0, "meter": 1.0, "meters": 1.0, "metre": 1.0, "metres": 1.0,
    "km": 1000.0, "kilometer": 1000.0, "kilometers": 1000.0, "kilometre": 1000.0, "kilometres": 1000.0,
    "in": 0.0254, "inch": 0.0254, "inches": 0.0254,
    "ft": 0.3048, "foot": 0.3048, "feet": 0.3048,
    "yd": 0.9144, "yard": 0.9144, "yards": 0.9144,
    "mi": 1609.344, "mile": 1609.344, "miles": 1609.344,
}


def metres_per_unit(label):
    """Metres per unit of a sisir_unit label, or None for an unknown one (or none at all)."""
    if not label:
        return None
    return UNIT_METRES.get(str(label).strip().lower())


def f32(value):
    """The float32 nearest to `value`, as a Python float."""
    return struct.unpack("<f", struct.pack("<f", float(value)))[0]


def _coord(value):
    c = f32(value)
    return "%.6f" % (0.0 if c == 0.0 or abs(c) < 5e-7 else c)  # never "-0.000000"


def color8(c):
    """A colour channel in 8-bit steps, as 3ds Max's wire colours hold it."""
    return int(min(max(float(c), 0.0), 1.0) * 255.0 + 0.5) / 255.0


def assign_layer_ids(layers):
    """`layers`: [(key, sisir_id or None, name, (r, g, b), visible)] in order of first appearance.
    Returns {key: (id, name, colour, visible)}: a layer an import made keeps Sisir's id, any other
    takes the next free one after them."""
    used = set(l[1] for l in layers if l[1] is not None and l[1] >= 0)
    nxt = max(used) + 1 if used else 0
    out = {}
    for key, sid, name, color, visible in layers:
        if key in out:
            continue
        if sid is None or sid < 0 or sid in [v[0] for v in out.values()]:
            sid = nxt
            nxt += 1
        out[key] = (sid, name, color, visible)
    return out


def remembered_unit(settings_application, default):
    """The unit Export Guides (PLY) last asked for in a plug-in (PlyExportDialog's QSettings, in the
    registry on Windows), or `default`."""
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Software\\Sisir\\" + settings_application) as key:
            value = str(winreg.QueryValueEx(key, "plyExportUnits")[0])
        return value if value in EXPORT_UNITS else default
    except (ImportError, OSError):
        return default


def write(path, layers, curves, unit):
    """`layers`: {id: (name, (r, g, b), visible)}; `curves`: [(layer id, [(x, y, z), ...])] in
    Sisir's Y-up, in `unit` (mm, cm or m), at least two points each. Curves go out sorted by layer
    id (stable). Returns (curves written, points written)."""
    if unit not in EXPORT_UNITS:
        raise ValueError("unit must be mm, cm or m, not %r" % (unit,))
    curves = [c for c in curves if len(c[1]) >= 2]
    curves.sort(key=lambda c: c[0])
    used = sorted(set(c[0] for c in curves))
    count = sum(len(c[1]) for c in curves)
    lines = ["ply", "format ascii 1.0", "comment sisir_unit " + unit]
    for lid in used:
        name, color, visible = layers.get(lid, ("Layer %d" % lid, (1.0, 1.0, 1.0), True))
        name = name.replace('"', "'").replace("\r", " ").replace("\n", " ")
        lines.append('comment layer %d "%s" %.6f %.6f %.6f %d' % (
            lid, name, color8(color[0]), color8(color[1]), color8(color[2]), 1 if visible else 0))
    lines += ["element vertex %d" % count, "property float x", "property float y", "property float z",
              "property uchar anchor", "property int layer_id", "property int curve_id", "end_header"]
    for cid, (lid, pts) in enumerate(curves):
        for i, p in enumerate(pts):
            lines.append("%s %s %s %d %d %d" % (_coord(p[0]), _coord(p[1]), _coord(p[2]), 1 if i == 0 else 0, lid, cid))
    with io.open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(u"\n".join(lines) + u"\n")
    return len(curves), count
