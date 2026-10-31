"""Export Guides (PLY): the selected curves as Sisir guides, written by sisir_ply.py -- the
writer Sisir for Maya and Sisir for 3ds Max use too, so the same guides make the same file from any
of them: Sisir's Y-up in the unit asked for (mm, cm or m), each Sisir guide layer an import made
keeping its id, name and colour, knots as they are.

Curves an import made carry the scale they came in at; curves drawn later take the scene's.
"""

import time

import numpy as np

from . import hair as _hair
from . import sisir_ply


def _is_guide(obj):
    return obj.type == "CURVE" or (obj.type == "CURVES" and not _hair.is_hair(obj))


def sources(context):
    """The selected curve objects, and the visible ones under selected objects."""
    out, seen = [], set()
    selected = list(context.selected_objects)
    for obj in selected:
        if _is_guide(obj) and obj.name not in seen:
            seen.add(obj.name)
            out.append(obj)
    for obj in selected:
        for child in obj.children_recursive:
            if _is_guide(child) and child.visible_get() and child.name not in seen:
                seen.add(child.name)
                out.append(child)
    return out


def _units_per_metre(obj, scene):
    for holder in (obj, scene):
        try:
            v = float(holder.get("sisir_units_per_metre", 0.0))
        except (TypeError, ValueError):
            v = 0.0
        if v > 1e-12:
            return v
    scale = float(scene.unit_settings.scale_length)
    return 1.0 / scale if scale > 1e-8 else 1.0


def _object_strands(obj):
    """Each open curve's points in object space; closed ones are counted and skipped."""
    strands, closed = [], 0
    if obj.type == "CURVE":
        for spline in obj.data.splines:
            if spline.use_cyclic_u:
                closed += 1
                continue
            if spline.type == "BEZIER":
                pts = np.empty(len(spline.bezier_points) * 3, dtype=np.float32)
                spline.bezier_points.foreach_get("co", pts)
                pts = pts.reshape(-1, 3)
            else:
                co = np.empty(len(spline.points) * 4, dtype=np.float32)
                spline.points.foreach_get("co", co)
                pts = co.reshape(-1, 4)[:, :3]
            if len(pts) >= 2:
                strands.append(pts)
    else:
        c = obj.data
        sizes = np.empty(len(c.curves), dtype=np.int32)
        c.curves.foreach_get("points_length", sizes)
        pos = np.empty(len(c.points) * 3, dtype=np.float32)
        c.attributes["position"].data.foreach_get("vector", pos)
        pos = pos.reshape(-1, 3)
        cyclic = c.attributes.get("cyclic")
        flags = np.zeros(len(sizes), dtype=bool)
        if cyclic is not None and cyclic.domain == "CURVE":
            cyclic.data.foreach_get("value", flags)
        offset = 0
        for i, n in enumerate(sizes.tolist()):
            if flags[i]:
                closed += 1
            elif n >= 2:
                strands.append(pos[offset:offset + n])
            offset += n
    return strands, closed


def _layer(obj):
    """(key, Sisir layer id or None, name, colour, visible). A curve object an import made is one
    Sisir guide layer by itself; any other takes its collection's name."""
    color = tuple(obj.color[:3])
    name = obj.get("sisir_layer_name")
    if name:
        try:
            sid = int(obj.get("sisir_layer_id"))
        except (TypeError, ValueError):
            sid = None
        return ("sisir", sid, str(name)), sid, str(name), color, obj.visible_get()
    collection = obj.users_collection[0] if obj.users_collection else None
    if collection is not None:
        return ("blender", collection.name), None, collection.name, color, not collection.hide_viewport and not collection.hide_render
    return ("blender", obj.name), None, obj.name, color, obj.visible_get()


def remembered_unit():
    """The unit Export Guides (PLY) last asked for here (SisirBridge's popup), metres before that."""
    return sisir_ply.remembered_unit("SisirForBlender", "m")


def export(context, path, unit):
    objects = sources(context)
    if not objects:
        raise RuntimeError("No selected objects hold curves")
    start = time.time()
    infos = [_layer(obj) for obj in objects]
    ids = sisir_ply.assign_layer_ids(infos)
    layers = {v[0]: (v[1], v[2], v[3]) for v in ids.values()}
    strands, closed = [], 0
    for obj, info in zip(objects, infos):
        # Blender units -> the export unit in one factor, so equal choices give the same file from
        # any DCC (and an unscaled import's numbers stay exact).
        k = 1.0 / (_units_per_metre(obj, context.scene) * sisir_ply.metres_per_unit(unit))
        m = np.array(obj.matrix_world, dtype=np.float64)
        object_strands, object_closed = _object_strands(obj)
        closed += object_closed
        for pts in object_strands:
            world = pts.astype(np.float64) @ m[:3, :3].T + m[:3, 3]
            # Z-up -> Sisir's Y-up: the inverse of the import's (x, -z, y).
            sisir = np.stack([world[:, 0] * k, world[:, 2] * k, -world[:, 1] * k], axis=1)
            if np.isfinite(sisir).all():
                strands.append((ids[info[0]][0], sisir.tolist()))
    if not strands:
        raise RuntimeError("No valid open guide strands with at least two points were found")
    written, vertex_count = sisir_ply.write(path, layers, strands, unit)
    message = f"Exported {written} Sisir guides ({vertex_count} points) from Blender in {time.time() - start:.2f}s"
    if closed:
        message += f" ({closed} closed curves skipped)"
    return message, written, len(layers)
