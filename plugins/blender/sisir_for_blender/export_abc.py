"""Export Hair (ABC): the selected Sisir hair as Sisir's own Alembic groom.

Blender only gathers the strands (world space, back in Sisir's metres and Y-up, widths as drawn);
SisirBridge shows Sisir's Export Hair (ABC) dialog and writes the file with the writer Sisir itself
uses (src/ExportAbcWriter.cpp), so the layout, names and attribute scopes are exactly Sisir's.
"""

import os
import struct
import tempfile

import numpy as np

from . import hair as _hair

SELECT_HAIR = "Select the Sisir hair to export, or the Sisir import it belongs to."


def targets(context):
    """What Sisir for 3ds Max exports for the same selection (ExportAbc.cpp resolveTargets): the
    selected hair, the hair directly under a selected object, and the visible hair deeper down."""
    out, seen = [], set()

    def add(obj):
        if _hair.is_hair(obj) and obj.name not in seen:
            seen.add(obj.name)
            out.append(obj)

    selected = list(context.selected_objects)
    for obj in selected:
        add(obj)
    for obj in selected:
        for child in obj.children:
            add(child)
        for child in obj.children_recursive:
            if child.parent is not obj and child.visible_get():
                add(child)
    # Sisir's order (Blender lists children by name): an import exports as Sisir itself would.
    out.sort(key=lambda o: (o.sisir_hair.source_file, o.sisir_hair.set_index))
    return out


def _strands(obj):
    c = obj.data
    s = obj.sisir_hair
    n_curves, n_points = len(c.curves), len(c.points)
    sizes = np.empty(n_curves, dtype=np.int32)
    c.curves.foreach_get("points_length", sizes)
    pos = np.empty(n_points * 3, dtype=np.float32)
    c.attributes["position"].data.foreach_get("vector", pos)
    radius = np.full(n_points, 0.0, dtype=np.float32)
    if "radius" in c.attributes:
        c.attributes["radius"].data.foreach_get("value", radius)
    uv = np.zeros(n_curves * 2, dtype=np.float32)
    if "root_uv" in c.attributes:
        c.attributes["root_uv"].data.foreach_get("vector", uv)
    clump = np.arange(n_curves, dtype=np.int32)
    if "clump_id" in c.attributes:
        c.attributes["clump_id"].data.foreach_get("value", clump)

    m = np.array(obj.matrix_world, dtype=np.float64)
    upm = s.units_per_metre if s.units_per_metre > 1e-12 else 1.0
    world = pos.reshape(-1, 3).astype(np.float64) @ m[:3, :3].T + m[:3, 3]
    world /= upm
    sisir = np.empty_like(world)
    sisir[:, 0] = world[:, 0]
    sisir[:, 1] = world[:, 2]
    sisir[:, 2] = -world[:, 1]
    # Strand widths follow the transform's uniform scale, as the viewport and the render draw them.
    width_scale = abs(np.linalg.det(m[:3, :3])) ** (1.0 / 3.0) * s.width_scale / upm
    widths = radius.astype(np.float64) * 2.0 * width_scale
    return sizes, sisir.astype(np.float32), widths.astype(np.float32), uv, clump


def write_dump(objects, path):
    """The hair as SisirBridge's export-abc reads it (SisirBridge.cpp, readDump). Returns the
    number of strands and the longest strand's point count."""
    strands = longest = 0
    with open(path, "wb") as f:
        f.write(b"SISIRABC")
        f.write(struct.pack("<3I", 1, len(objects), 0))
        for obj in objects:
            s = obj.sisir_hair
            sizes, pos, widths, uv, clump = _strands(obj)
            name = (s.hair_set or obj.name).encode("utf-8")[:4096]
            f.write(struct.pack("<I", len(name)))
            f.write(name)
            f.write(struct.pack("<2I", len(sizes), len(widths)))
            f.write(sizes.astype("<i4").tobytes())
            f.write(pos.astype("<f4").tobytes())
            f.write(widths.astype("<f4").tobytes())
            f.write(uv.astype("<f4").tobytes())
            f.write(clump.astype("<i4").tobytes())
            f.write(struct.pack("<5f", s.set_color[0], s.set_color[1], s.set_color[2], s.groom_roughness, s.groom_ao))
            strands += len(sizes)
            if len(sizes):
                longest = max(longest, int(sizes.max()))
        f.seek(16)
        f.write(struct.pack("<I", longest))
    return strands, longest


def dump_path():
    return os.path.join(tempfile.gettempdir(), "sisir_abc_%d.bin" % os.getpid())
