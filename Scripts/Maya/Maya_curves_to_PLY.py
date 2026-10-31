# -*- coding: utf-8 -*-
"""HairTool curve exporter for Maya (Python 2/3)

Select NURBS curves, or groups containing them, run this script, and it will
export an ASCII PLY in the exact format HairTool expects (with layers). Also the
Sisir menu's Export Guides (PLY)... in the Sisir for Maya plug-in.

The file is written by sisir_ply.py (next to this script), the writer
Sisir for 3ds Max and Sisir for Blender use too: Sisir's Y-up, in the unit asked
for (mm, cm or m: Sisir for Maya's popup, sisirPlyUnit, remembered).

Notes:
- Positions come from Maya's API, in its internal unit, read in what the Sisir
  import recorded on its root (sisirMetresPerInternal: its Units choice), else
  Maya's own centimetres -- whatever the scene's linear unit is.
- Each curve becomes one HairTool guide (curve_id): curves selected directly,
  and every visible curve in the hierarchies under selected groups.
- The group a curve sits in is its layer (layer_id), as Sisir's Maya import
  lays guides out (<scene>_Guides > layer group > GuideCurve_N). A group the
  import made keeps Sisir's layer id, name and colour (its sisirLayerId and
  sisirLayerName attributes), and its curves go out as the points Sisir's
  guides have: a cubic guide's edit points, not its control vertices.
"""

from __future__ import print_function

try:
    xrange  # type: ignore[name-defined]
except NameError:
    xrange = range  # type: ignore[assignment]

import os
import sys
import time

import maya.cmds as cmds
import maya.api.OpenMaya as om

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sisir_ply  # noqa: E402


def _is_visible(node):
    sl = om.MSelectionList()
    sl.add(node)
    return sl.getDagPath(0).isVisible()


def _selected_curve_shapes():
    """nurbsCurve shapes to export, by full path, in scene order: the curves selected
    directly (shape or transform), visible or not, and every visible curve in the
    hierarchies under the other selected nodes."""
    out = []
    seen = set()

    def add(shape):
        if shape in seen or cmds.getAttr(shape + '.intermediateObject'):
            return
        seen.add(shape)
        out.append(shape)

    for node in cmds.ls(sl=True, long=True) or []:
        if cmds.nodeType(node) == 'nurbsCurve':
            add(node)
            continue
        if not cmds.objectType(node, isAType='transform'):
            continue
        for shape in cmds.listRelatives(node, shapes=True, type='nurbsCurve', fullPath=True) or []:
            add(shape)
        for shape in cmds.ls(node, dag=True, type='nurbsCurve', long=True) or []:
            if _is_visible(shape):
                add(shape)
    return out


def _metres_per_internal(shape):
    """What one internal unit of `shape` is in metres: what its Sisir import recorded, else 0.01."""
    node = shape
    while node:
        if cmds.attributeQuery('sisirMetresPerInternal', node=node, exists=True):
            try:
                value = float(cmds.getAttr(node + '.sisirMetresPerInternal'))
                if value > 0.0:
                    return value
            except (TypeError, ValueError):
                pass
        node = (cmds.listRelatives(node, parent=True, fullPath=True) or [None])[0]
    return 0.01


def _layer_of(shape):
    """The group the curve's transform sits in (its full path), or None at the scene root."""
    transform = (cmds.listRelatives(shape, parent=True, fullPath=True) or [None])[0]
    group = (cmds.listRelatives(transform, parent=True, fullPath=True) or [None])[0] if transform else None
    return group


def _palette_color(idx):
    # Simple HSV->RGB with golden-ratio hues for distinct colors (groups without a colour of their own)
    h = (idx * 0.61803398875) % 1.0
    s = 0.65
    v = 0.95
    i = int(h * 6.0)
    f = h * 6.0 - i
    p = v * (1.0 - s)
    q = v * (1.0 - s * f)
    t = v * (1.0 - s * (1.0 - f))
    i = i % 6
    if i == 0:
        return (v, t, p)
    if i == 1:
        return (q, v, p)
    if i == 2:
        return (p, v, t)
    if i == 3:
        return (p, q, v)
    if i == 4:
        return (t, p, v)
    return (v, p, q)


def _layer_info(group, index):
    """(key, Sisir layer id or None, name, colour, visible, from Sisir) of a curve's layer group."""
    if group is None:
        return ('maya', None), None, 'Curves', _palette_color(index), True, False
    color = None
    if cmds.getAttr(group + '.overrideEnabled') and cmds.getAttr(group + '.overrideRGBColors'):
        color = tuple(cmds.getAttr(group + '.overrideColorRGB')[0])
    if cmds.attributeQuery('sisirLayerName', node=group, exists=True):
        name = cmds.getAttr(group + '.sisirLayerName') or ''
        try:
            sid = int(cmds.getAttr(group + '.sisirLayerId'))
        except (TypeError, ValueError):
            sid = None
        return ('sisir', sid, name), sid, name, color or _palette_color(index), _is_visible(group), True
    name = (cmds.ls(group, shortNames=True) or [group])[0]
    return ('maya', group), None, name, color or _palette_color(index), _is_visible(group), False


def _stored_guide_points(curve_dag):
    """The points Sisir's import built a cubic guide through, or None."""
    transform = om.MDagPath(curve_dag)
    transform.pop()
    node = om.MFnDependencyNode(transform.node())
    if not node.hasAttribute('sisirGuidePoints'):
        return None
    data = node.findPlug('sisirGuidePoints', False).asMObject()
    if data.isNull():
        return None
    return list(om.MFnPointArrayData(data).array())


def _curve_world_points(curve_dag, mode='cvs', samples_per_span=4, min_samples=2):
    """Return list of (x,y,z) world positions, in centimetres, for exporting.

    mode:
      - 'cvs' (default): export the curve CVs (control vertices). This preserves
        the exact point count you see in Maya.
      - 'eps': the edit points, the points a curve built through them passes
        through (Sisir's cubic guides).
      - 'sampled': uniformly sample along the curve.
    """
    fn = om.MFnNurbsCurve(curve_dag)

    if mode == 'cvs' or (mode == 'eps' and fn.degree <= 1):
        cvs = fn.cvPositions(om.MSpace.kWorld)
        return [(p.x, p.y, p.z) for p in cvs]

    if mode == 'eps':
        params = []
        for k in fn.knots():
            if not params or k > params[-1]:
                params.append(k)
        local = [fn.getPointAtParam(u, om.MSpace.kObject) for u in params]
        world = curve_dag.inclusiveMatrix()
        # Sisir's import keeps a cubic guide's points (sisirGuidePoints): while the curve still
        # passes through them they go out exactly, not as the spline solve gives them back.
        stored = _stored_guide_points(curve_dag)
        if stored is not None and len(stored) == len(local) and all(
                (a - b).length() <= 1e-6 * max(1.0, om.MVector(b).length()) for a, b in zip(local, stored)):
            local = stored
        pts = []
        for p in local:
            q = p * world
            pts.append((q.x, q.y, q.z))
        return pts

    # Fallback: uniform sampling
    spans = fn.numSpans

    sample_count = max(int(spans) * int(samples_per_span) + 1, int(min_samples))
    if sample_count < 2:
        sample_count = 2

    pmin, pmax = fn.knotDomain
    if pmax <= pmin:
        pmin = 0.0
        pmax = 1.0

    pts = []
    for i in xrange(sample_count):
        t = float(i) / float(sample_count - 1)
        param = pmin + (pmax - pmin) * t
        p = fn.getPointAtParam(param, om.MSpace.kWorld)
        pts.append((p.x, p.y, p.z))

    return pts


def _tell(message):
    if cmds.about(batch=True):
        cmds.warning(message)
    else:
        cmds.confirmDialog(title='Export Guides (PLY)', message=message, button=['OK'])


def export_selected_curves_to_ply(mode='cvs', samples_per_span=4, min_samples=2, path=None, unit=None):
    """Export the selected curves (see _selected_curve_shapes). Asks where to save unless `path`
    is given, then in which unit (mm, cm or m) unless `unit` is: Sisir for Maya's popup, which then
    exports by itself (this returns None), or the unit asked last when called with a `path`.
    Returns the file written, or None."""
    shapes = _selected_curve_shapes()
    if not shapes:
        _tell('Select the guide curves to export, or a group that contains them.')
        return None

    if path is None:
        file_filter = 'PLY Files (*.ply);;All Files (*.*)'
        result = cmds.fileDialog2(fileFilter=file_filter, dialogStyle=2, fileMode=0, caption='Export Guides')
        if not result:
            return None
        path = result[0]
        if not path.lower().endswith('.ply'):
            path = path + '.ply'
        if unit is None and hasattr(cmds, 'sisirPlyUnit'):
            cmds.sisirPlyUnit(file=path)  # asks once this returns, then exports in that unit
            return None
    if not path.lower().endswith('.ply'):
        path = path + '.ply'
    if unit is None:
        unit = sisir_ply.remembered_unit('SisirForMaya', 'cm')
    unit_metres = sisir_ply.metres_per_unit(unit)

    start_time = time.time()

    # Layers: the groups the curves sit in, in order of first appearance.
    groups = [_layer_of(shape) for shape in shapes]
    infos = {}
    for group in groups:
        if group not in infos:
            infos[group] = _layer_info(group, len(infos))
    ids = sisir_ply.assign_layer_ids([i[:5] for i in infos.values()])
    layers = dict((v[0], (v[1], v[2], v[3])) for v in ids.values())

    curves = []
    for shape, group in zip(shapes, groups):
        info = infos[group]
        sl = om.MSelectionList()
        sl.add(shape)
        # A guide Sisir imported goes out as Sisir's points; any other curve as asked.
        pts = _curve_world_points(sl.getDagPath(0), mode='eps' if info[5] and mode == 'cvs' else mode,
                                  samples_per_span=samples_per_span, min_samples=min_samples)
        # Internal units -> the export unit in one factor, so equal choices give the same file
        # from any DCC.
        factor = _metres_per_internal(shape) / unit_metres
        if len(pts) >= 2:
            curves.append((ids[info[0]][0], [(p[0] * factor, p[1] * factor, p[2] * factor) for p in pts]))

    if not curves:
        _tell('The selected curves have no points to export.')
        return None

    written, vertex_count = sisir_ply.write(path, layers, curves, unit)
    print('Exported {0} curves ({1} vertices) to: {2} ({3:.2f}s)'.format(written, vertex_count, path, time.time() - start_time))
    return path


if __name__ == '__main__':
    export_selected_curves_to_ply()
