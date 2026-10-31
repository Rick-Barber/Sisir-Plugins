"""Builds a baked Sisir scene in Blender: plugins/max/src/SceneBuilder.cpp, step by step.

`Builder.steps()` yields after each object it makes -- (fraction, label), on Max's progress scale --
so the import operator builds one step per timer tick and Blender keeps drawing (and the dialog
keeps showing the progress) however big the groom. Every ID it makes is listed in `created`, so a
cancelled or failed build leaves nothing behind.

Coordinates: Sisir is Y-up in metres; Blender is Z-up: (x, y, z) -> (x·s, -z·s, y·s), baked into
the data, identity transforms everywhere, s Blender units per Sisir metre.
"""

import os
import re

import bpy
import numpy as np

from . import hair as _hair
from .materials import Materials
from .options import UNIT_METRES

# Past this many points a groom is drawn thinned by default (Display % brings it back); renders
# always use every strand. The same budget as Sisir for 3ds Max.
VIEWPORT_POINT_BUDGET = 6.0e6


def legal_name(name, fallback="Sisir"):
    """ASCII letters, digits and '_' (Sisir for 3ds Max's legalName), so every plug-in names alike."""
    out = re.sub(r"[^A-Za-z0-9_]", "_", str(name))
    while "__" in out:
        out = out.replace("__", "_")
    return fallback if out in ("", "_") else out


def _layer_collection(layer, collection):
    if layer.collection == collection:
        return layer
    for child in layer.children:
        found = _layer_collection(child, collection)
        if found is not None:
            return found
    return None


def layer_shown(layer):
    """A META layer entry as Sisir showed it: its own eye, and every Layers panel folder above it
    showing (MetaFolders.h metaLayerShown). What an import takes by default."""
    if not bool(layer.get("visible", True)):
        return False
    return all(bool(f.get("visible", True)) for f in (layer.get("folders") or []))


class Report:
    def __init__(self):
        self.warnings = []
        self.hair_sets = 0
        self.hair_strands = 0
        self.guide_curves = 0
        self.card_layers = 0
        self.meshes = 0
        self.materials = 0
        self.root = None


class Builder:
    def __init__(self, context, bake, options, assets_dir, source_file):
        self.context = context
        self.bake = bake
        self.meta = bake.meta
        self.options = options
        self.assets_dir = assets_dir
        self.source_file = source_file
        self.scene_name = legal_name(self.meta.get("sceneName", ""), "Sisir")
        # Blender units = Sisir metres in the chosen unit (ImportOptions.unit).
        unit = options.resolved_unit()
        if unit in UNIT_METRES:
            self.s = 1.0 / UNIT_METRES[unit]
        elif unit == "scene":
            scale_length = float(context.scene.unit_settings.scale_length)
            self.s = 1.0 / scale_length if scale_length > 1e-8 else 1.0
        else:
            # "mesh": the user's mesh file numbers, as Blender units: Sisir metres / importScale.
            import_scale = float((self.meta.get("mesh") or {}).get("importScale", 0.01))
            self.s = 1.0 / import_scale if import_scale > 1e-12 else 100.0
        self.created = []
        self.report = Report()
        self.materials = Materials(self.meta, assets_dir, self.scene_name, self.s, self.created)
        self._collections = {}

    # --- containers ------------------------------------------------------------------------------

    def _collection(self, suffix):
        if suffix in self._collections:
            return self._collections[suffix]
        c = bpy.data.collections.new(self.scene_name + suffix)
        self.created.append(c)
        if suffix == "_Sisir":
            self.context.scene.collection.children.link(c)
        else:
            self._collection("_Sisir").children.link(c)
        self._collections[suffix] = c
        return c

    def _folder_collection(self, suffix, layer):
        """The collection a layer's object goes in: the kind's (_Guides / _Cards), or the nested
        collections named after its Layers panel folders (META "folders", outermost first), each made
        once and hidden when its folder's eye was off -- the plug-ins' shared MetaFolders.h rules."""
        parent = self._collection(suffix)
        key = suffix
        for f in layer.get("folders") or []:
            name = str(f.get("name") or "Folder")
            key += "\x1f" + name
            c = self._collections.get(key)
            if c is None:
                c = bpy.data.collections.new(name)
                self.created.append(c)
                parent.children.link(c)
                if not bool(f.get("visible", True)):
                    c.hide_render = True
                    lc = _layer_collection(self.context.view_layer.layer_collection, c)
                    if lc is not None:
                        lc.hide_viewport = True
                self._collections[key] = c
            parent = c
        return parent

    def _adopt(self, obj, hidden=False):
        obj.parent = self.report.root
        if hidden:
            # Hidden as Sisir had it: out of the viewport and the render, back with the eye.
            obj.hide_render = True
            try:
                obj.hide_set(True)
            except RuntimeError:
                pass

    @staticmethod
    def _color(c, fallback=0.8):
        try:
            return (float(c[0]), float(c[1]), float(c[2]), 1.0)
        except (TypeError, IndexError, ValueError):
            return (fallback, fallback, fallback, 1.0)

    # --- the steps -------------------------------------------------------------------------------

    def steps(self):
        o = self.options
        root = bpy.data.objects.new(self.scene_name + "_Sisir", None)
        self.created.append(root)
        root.empty_display_size = 0.01 * self.s  # a handle to pick the import by, not a frame around it
        root["sisir_source_file"] = self.source_file
        root["sisir_units_per_metre"] = self.s
        self._collection("_Sisir").objects.link(root)
        self.report.root = root
        # For curves drawn later in this scene: Export Guides (PLY) reads them in the same units.
        self.context.scene["sisir_units_per_metre"] = self.s

        if o.mesh:
            yield 0.05, "Mesh"
            self._import_mesh("mesh", "_Mesh")
        if o.refMesh:
            yield 0.15, "Reference mesh"
            self._import_mesh("refMesh", "_RefMesh")
        if o.guides:
            yield 0.25, "Guides"
            yield from self._guides()
        if o.hair:
            yield 0.35, "Hair"
            yield from self._hair_sets()
        if o.cards:
            yield 0.85, "Cards"
            yield from self._cards()
            self._raise_transparent_bounces()
        yield 1.0, "Done"
        # The view layer can still list objects deleted just before the import, as None, until it
        # resyncs (Blender 5.x); resyncing here would evaluate the whole groom for nothing.
        for obj in self.context.view_layer.objects:
            if obj is not None and obj.select_get():
                obj.select_set(False)
        try:
            root.select_set(True)
            self.context.view_layer.objects.active = root
        except RuntimeError:
            pass

    def build(self, progress=None):
        """All steps at once (scripted imports). `progress(f, label)` may return False to cancel."""
        for f, label in self.steps():
            if progress is not None and progress(f, label) is False:
                return False
        return True

    def discard(self):
        """Removes everything this build made (a cancel or a failure)."""
        removers = {
            bpy.types.Object: bpy.data.objects, bpy.types.Mesh: bpy.data.meshes, bpy.types.Curves: bpy.data.hair_curves,
            bpy.types.Curve: bpy.data.curves, bpy.types.Material: bpy.data.materials, bpy.types.Image: bpy.data.images,
            bpy.types.Collection: bpy.data.collections,
        }
        # Objects first: their data and materials are then free to go.
        order = sorted(self.created, key=lambda i: 0 if isinstance(i, bpy.types.Object) else 1)
        for item in order:
            for kind, coll in removers.items():
                if isinstance(item, kind):
                    try:
                        coll.remove(item)
                    except (ReferenceError, RuntimeError):
                        pass
                    break
        self.created = []

    # --- meshes ----------------------------------------------------------------------------------

    def _import_mesh(self, key, suffix):
        m = self.meta.get(key) or {}
        if not m.get("present", False):
            return
        rel = m.get("file", "")
        path = os.path.join(self.assets_dir, rel) if rel else ""
        label = self.scene_name + suffix
        if not path or not os.path.isfile(path):
            self.report.warnings.append("%s: the mesh file is missing from the bake" % label)
            return
        collection = self._collection(suffix)
        view_layer = self.context.view_layer
        layer = _layer_collection(view_layer.layer_collection, collection)
        previous = view_layer.active_layer_collection
        if layer is not None:
            view_layer.active_layer_collection = layer
        objects_before = set(bpy.data.objects)
        meshes_before = set(bpy.data.meshes)
        materials_before = set(bpy.data.materials)
        images_before = set(bpy.data.images)
        try:
            # File numbers -> Sisir metres -> Blender units; OBJ's Y-up -> Z-up is (x, -z, y), as above.
            result = bpy.ops.wm.obj_import(filepath=path, global_scale=float(m.get("importScale", 0.01)) * self.s,
                                           forward_axis="NEGATIVE_Z", up_axis="Y", use_split_objects=True,
                                           use_split_groups=True, import_vertex_groups=False, validate_meshes=True)
        except RuntimeError as e:
            result = {"CANCELLED"}
            self.report.warnings.append("%s: %s" % (label, str(e).strip()))
        finally:
            view_layer.active_layer_collection = previous
        new_objects = [obj for obj in bpy.data.objects if obj not in objects_before]
        self.created.extend(new_objects)
        self.created.extend(me for me in bpy.data.meshes if me not in meshes_before)
        # The OBJ's own materials go: the import makes its own, from Sisir's material (as in Maya).
        for mat in [mt for mt in bpy.data.materials if mt not in materials_before]:
            bpy.data.materials.remove(mat)
        for img in [im for im in bpy.data.images if im not in images_before and im.users == 0]:
            bpy.data.images.remove(img)
        if "FINISHED" not in result or not new_objects:
            if not self.report.warnings or not self.report.warnings[-1].startswith(label):
                self.report.warnings.append("%s: the mesh could not be read" % label)
            return
        material = self.materials.mesh(key == "refMesh", m.get("texture", "")) if self.options.materials else None
        if material is not None:
            self.report.materials += 1
        for obj in new_objects:
            if obj.type != "MESH":
                continue
            obj.data.materials.clear()
            if material is not None:
                obj.data.materials.append(material)
            self._adopt(obj)
            self.report.meshes += 1

    # --- guides: one curve object per layer, one spline per guide --------------------------------

    def _guides(self):
        layers = self.meta.get("guideLayers") or []
        s = self.s
        cubic = self.options.guideDegree == 3
        for li, l in enumerate(layers):
            name = l.get("name", "")
            visible = bool(l.get("visible", True))
            if not self.options.selected(self.options.guideLayers, name, layer_shown(l)):
                continue
            g = self.bake.guides(int(l.get("chunk", -1)))
            if g is None or g["curves"] == 0:
                continue
            collection = self._folder_collection("_Guides", l)
            data = bpy.data.curves.new(legal_name(name), "CURVE")
            self.created.append(data)
            data.dimensions = "3D"
            p = g["positions"].reshape(-1, 3)
            pts = np.empty_like(p)
            pts[:, 0] = p[:, 0] * s
            pts[:, 1] = -p[:, 2] * s
            pts[:, 2] = p[:, 1] * s
            offset = 0
            for n in g["pointCounts"].tolist():
                chunk = pts[offset:offset + n]
                offset += n
                if cubic:
                    # A smooth curve through Sisir's points (Max's Auto knots).
                    spline = data.splines.new("BEZIER")
                    spline.bezier_points.add(n - 1)
                    spline.bezier_points.foreach_set("co", chunk.ravel())
                    # Enums have no foreach_set; setting the type also computes the handles.
                    for bp in spline.bezier_points:
                        bp.handle_left_type = "AUTO"
                        bp.handle_right_type = "AUTO"
                else:
                    spline = data.splines.new("POLY")
                    spline.points.add(n - 1)
                    co = np.ones((n, 4), dtype=np.float32)
                    co[:, :3] = chunk
                    spline.points.foreach_set("co", co.ravel())
                self.report.guide_curves += 1
            obj =bpy.data.objects.new(legal_name(name), data)
            self.created.append(obj)
            collection.objects.link(obj)
            obj.color = self._color(l.get("color"))
            # As Scripts/Blender tags them; Export Guides (PLY) keeps each as its own layer.
            obj["sisir_layer_id"] = li
            obj["sisir_layer_name"] = name
            obj["sisir_source_path"] = self.source_file
            obj["sisir_strand_count"] = int(g["curves"])
            obj["sisir_units_per_metre"] = s
            self._adopt(obj, hidden=not visible)
            yield -1.0, ""

    # --- hair ------------------------------------------------------------------------------------

    def _hair_sets(self):
        sets = self.meta.get("hairSets") or []
        o = self.options
        chosen = [h for h in sets if o.selected(o.hairSets, h.get("name", ""), bool(h.get("visible", True)))]
        total = float(sum(float(h.get("points", 0)) for h in chosen))
        budget = 100.0 * VIEWPORT_POINT_BUDGET / total if total > VIEWPORT_POINT_BUDGET else 100.0
        display = min(o.displayPercent, budget)
        if display < o.displayPercent - 0.01:
            self.report.warnings.append("Hair is shown at %.0f%% in the viewport to keep it responsive (Display %% on each hair "
                                        "object); renders use every strand." % display)
        materials = self.meta.get("hairMaterials") or []
        done = 0.0
        for h in chosen:
            name = h.get("name", "")
            set_index = sets.index(h)
            data = self.bake.hair(int(h.get("chunk", -1)))
            if data is None or data["strands"] == 0:
                continue
            collection = self._collection("_Hair")
            material_id = min(max(int(h.get("materialId", 0)), 0), 9)
            mat = materials[material_id] if material_id < len(materials) else {}
            color = self._color(h.get("color"))
            obj, curves = _hair.create_hair(legal_name(name), data, self.s, collection, {
                "display_percent": display,
                "material_id": material_id,
                "source_file": self.source_file,
                "hair_set": name,
                "set_index": set_index,
                # As Sisir's ABC export takes them from the set's render material.
                "groom_roughness": min(max(float(mat.get("roughness", 0.05)), 0.0), 1.0),
                "groom_ao": min(max(float(mat.get("hairAOIntensity", 1.0)), 0.0), 1.0) if mat.get("showHairAO", False) else 1.0,
                "set_color": color[:3],
                "units_per_metre": self.s,
            })
            self.created.append(curves)
            self.created.append(obj)
            if o.materials:
                curves.materials.append(self.materials.hair(material_id))
            self._adopt(obj, hidden=not bool(h.get("visible", True)))
            self.report.hair_sets += 1
            self.report.hair_strands += int(data["strands"])
            # Hair takes 35% to 85% of the bar, shared by point count.
            done += float(h.get("points", 0))
            yield 0.35 + 0.5 * min(done / max(total, 1.0), 1.0), "Hair: " + name

    # --- cards -----------------------------------------------------------------------------------

    def _cards(self):
        layers = self.meta.get("cardLayers") or []
        s = self.s
        o = self.options
        for li, l in enumerate(layers):
            name = l.get("name", "")
            visible = bool(l.get("visible", True))
            if not o.selected(o.cardLayers, name, layer_shown(l)):
                continue
            chunk = int(l.get("chunk", -1))
            k = self.bake.cards(chunk, o.smoothCards) or self.bake.cards(chunk, not o.smoothCards)
            if k is None or k["faces"] == 0:
                continue
            collection = self._folder_collection("_Cards", l)
            mesh = bpy.data.meshes.new(legal_name(name))
            self.created.append(mesh)
            p = k["positions"].reshape(-1, 3)
            pts = np.empty_like(p)
            pts[:, 0] = p[:, 0] * s
            pts[:, 1] = -p[:, 2] * s
            pts[:, 2] = p[:, 1] * s
            mesh.vertices.add(k["vertices"])
            mesh.vertices.foreach_set("co", pts.ravel())
            mesh.loops.add(k["corners"])
            corners = k["cornerIndices"].astype(np.int32)
            mesh.loops.foreach_set("vertex_index", corners)
            mesh.polygons.add(k["faces"])
            sizes = k["faceSizes"].astype(np.int32)
            starts = np.zeros(len(sizes), dtype=np.int32)
            if len(sizes) > 1:
                np.cumsum(sizes[:-1], out=starts[1:])
            mesh.polygons.foreach_set("loop_start", starts)
            if bpy.app.version < (4, 0, 0):
                mesh.polygons.foreach_set("loop_total", sizes)
            uv = mesh.uv_layers.new(name="UVMap")
            uv.data.foreach_set("uv", k["uvs"].reshape(-1, 2)[corners].ravel())
            mesh.update(calc_edges=True)
            n = k["normals"].reshape(-1, 3)
            normals = np.empty_like(n)
            normals[:, 0] = n[:, 0]
            normals[:, 1] = -n[:, 2]
            normals[:, 2] = n[:, 1]
            mesh.shade_smooth()
            mesh.normals_split_custom_set_from_vertices(normals.tolist())
            obj = bpy.data.objects.new(legal_name(name), mesh)
            self.created.append(obj)
            collection.objects.link(obj)
            color = self._color(l.get("color"))
            obj.color = color
            if o.materials:
                mesh.materials.append(self.materials.card(int(l.get("uvId", -1)), name, color[:3], legal_name))
            self._adopt(obj, hidden=not visible)
            self.report.card_layers += 1
            # Cards take the last 15% of the bar.
            yield 0.85 + 0.15 * (li + 1) / float(len(layers)), "Cards: " + name

    # Cycles stops a ray after Light Paths > Max Bounces > Transparent cut-out surfaces (8 by default)
    # and returns black, which is what dense card tips -- many cut-outs deep -- render as. Raised,
    # never lowered, when the cards are cut out; the viewport does not use it.
    TRANSPARENT_BOUNCES = 64

    def _raise_transparent_bounces(self):
        if self.report.card_layers <= 0 or not self.materials.card_alpha:
            return
        cycles = getattr(self.context.scene, "cycles", None)
        if cycles is None or not hasattr(cycles, "transparent_max_bounces"):
            return
        if cycles.transparent_max_bounces >= self.TRANSPARENT_BOUNCES:
            return
        before = cycles.transparent_max_bounces
        cycles.transparent_max_bounces = self.TRANSPARENT_BOUNCES
        self.report.warnings.append(
            "Cycles Transparent bounces raised from %d to %d so overlapping card cut-outs do not render "
            "black (Render Properties > Light Paths > Max Bounces); the viewport is not affected."
            % (before, self.TRANSPARENT_BOUNCES))

    def summary(self):
        r = self.report
        return "imported %s: %d hair sets (%s strands), %d guide curves, %d card layers, %d meshes" % (
            os.path.splitext(os.path.basename(self.source_file))[0], r.hair_sets, "{:,}".format(r.hair_strands),
            r.guide_curves, r.card_layers, r.meshes)
