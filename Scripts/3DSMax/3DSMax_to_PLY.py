"""Sisir guide exporter for 3ds Max Python.

Run this inside 3ds Max's Python environment. Select one or more Editable
Spline (or other spline shape) objects and run this script to export them as
a Sisir ASCII PLY guide file. Also the Sisir menu's Export Guides (PLY)... in
the Sisir for 3ds Max plug-in.

The file is written by sisir_ply.py (next to this script), the writer
Sisir for Maya and Sisir for Blender use too: Sisir's Y-up, in the unit asked
for (mm, cm or m; Sisir for 3ds Max's popup, remembered), with each Sisir guide
layer an import made keeping its id, name and colour. Spline numbers are read in
the unit the import recorded on the node (its Units choice), else the scene's
SYSTEM unit -- whatever unit the interface displays.
"""

import os
import sys
import time

try:
    import pymxs
except ImportError as exc:
    raise RuntimeError("This script must be run from 3ds Max's Python environment") from exc

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sisir_ply  # noqa: E402

rt = pymxs.runtime


def _metres_per_system_unit():
    """What one system unit is in metres (Customize > Units Setup > System Unit Setup)."""
    name = str(rt.units.SystemType).lstrip("#").lower()
    metres = sisir_ply.metres_per_unit(name)
    if metres is None:
        raise RuntimeError("Unknown 3ds Max system unit: %s" % name)
    return metres * float(rt.units.SystemScale)


def _metres_per_number(node, system_metres):
    """What one of the node's numbers is in metres: what a Sisir import recorded, else the system unit."""
    try:
        value = float(str(rt.getUserProp(node, "sisir_metres_per_unit")))
        if value > 0.0:
            return value
    except (TypeError, ValueError):
        pass
    return system_metres


def _layer_info(node):
    """(key, Sisir layer id or None, name, colour, visible) of the guide layer a shape belongs to.
    A shape Sisir for 3ds Max imported is one Sisir guide layer by itself."""
    color = (1.0, 1.0, 1.0)
    try:
        c = node.wirecolor
        color = (c.r / 255.0, c.g / 255.0, c.b / 255.0)
    except Exception:
        pass
    try:
        sisir_layer = rt.getUserProp(node, "sisir_layer_name")
        if sisir_layer:
            try:
                sid = int(str(rt.getUserProp(node, "sisir_layer_id")))
            except (TypeError, ValueError):
                sid = None
            return ("sisir", sid, str(sisir_layer)), sid, str(sisir_layer), color, not node.isHidden
    except Exception:
        pass
    try:
        layer = node.layer
        name = layer.name or "0"
        visible = not layer.isHidden
    except Exception:
        name = "0"
        visible = True
    return ("max", name), None, name, color, visible


def _iter_spline_objects(nodes):
    valid = []
    skipped = []
    for node in nodes:
        try:
            spline_count = rt.numSplines(node)
        except Exception:
            skipped.append(node)
            continue
        if spline_count and spline_count > 0:
            valid.append(node)
        else:
            skipped.append(node)
    return valid, skipped


def _node_strands(node, factor):
    """Each open spline's knots in Sisir's Y-up, multiplied by `factor` (numbers -> the export unit);
    closed splines are counted and skipped."""
    strands = []
    skipped_closed = 0
    spline_count = rt.numSplines(node)
    for spline_index in range(1, spline_count + 1):
        if rt.isClosed(node, spline_index):
            skipped_closed += 1
            continue
        knot_count = rt.numKnots(node, spline_index)
        if knot_count < 2:
            continue
        points = []
        for knot_index in range(1, knot_count + 1):
            local_point = rt.getKnotPoint(node, spline_index, knot_index)
            p = local_point * node.transform
            # Max is Z-up, Sisir Y-up: the inverse of the importer's (x, -z, y).
            points.append((p.x * factor, p.z * factor, -p.y * factor))
        strands.append(points)
    return strands, skipped_closed


def export_sisir_guides(path=None, unit=None):
    """Export the selected splines. Asks where (unless `path`) and in which unit (unless `unit`, or
    when scripted with a `path`: the unit asked last). Returns the file written, or None."""
    selection = list(rt.selection)
    if not selection:
        raise RuntimeError("Select at least one Editable Spline object to export")

    nodes, skipped_nodes = _iter_spline_objects(selection)
    # A selected group (a Sisir import's Guides dummy, say): the visible splines under it too.
    seen = set(rt.getHandleByAnim(n) for n in nodes)
    stack = [c for n in selection for c in n.children]
    while stack:
        child = stack.pop()
        stack.extend(child.children)
        handle = rt.getHandleByAnim(child)
        if handle in seen or child.isHidden:
            continue
        seen.add(handle)
        found, _ = _iter_spline_objects([child])
        nodes.extend(found)
    skipped_nodes = [n for n in skipped_nodes if not n.children]
    if not nodes:
        raise RuntimeError("No selected objects contain spline data (Editable Spline expected)")
    # Scene order, whatever order the selection or the walk above found them in.
    nodes.sort(key=lambda n: rt.getHandleByAnim(n))

    if not path:
        path = rt.getSaveFileName(
            caption="Export Sisir Guides",
            types="Sisir Guides (*.ply)|*.ply|All Files (*.*)|*.*|",
        )
        if not path:
            return None
        if unit is None and rt.execute("sisir != undefined"):
            unit = str(rt.sisir.plyUnit())  # Sisir for 3ds Max's popup; empty when cancelled
            if not unit:
                return None
    if unit is None:
        unit = sisir_ply.remembered_unit("SisirForMax", "cm")

    start_time = time.time()
    system_metres = _metres_per_system_unit()
    unit_metres = sisir_ply.metres_per_unit(unit)

    infos = [_layer_info(node) for node in nodes]
    ids = sisir_ply.assign_layer_ids(infos)
    layers = {v[0]: (v[1], v[2], v[3]) for v in ids.values()}
    strands = []
    skipped_closed = 0
    for node, info in zip(nodes, infos):
        # Numbers -> the export unit in one factor, so equal choices give the same file from any DCC.
        node_strands, node_skipped_closed = _node_strands(node, _metres_per_number(node, system_metres) / unit_metres)
        skipped_closed += node_skipped_closed
        lid = ids[info[0]][0]
        strands.extend((lid, points) for points in node_strands)

    if not strands:
        raise RuntimeError("No valid open guide strands with at least two points were found")
    written, vertex_count = sisir_ply.write(path, layers, strands, unit)

    elapsed = time.time() - start_time
    message = f"Exported {written} Sisir guides ({vertex_count} points) from 3ds Max in {elapsed:.2f}s"
    if skipped_nodes:
        message += f" ({len(skipped_nodes)} non-spline objects skipped)"
    if skipped_closed:
        message += f" ({skipped_closed} closed splines skipped)"
    print(message)
    try:
        rt.messageBox(message, title="Sisir Export")
    except Exception:
        pass
    return path


if __name__ == "__main__":
    export_sisir_guides()
