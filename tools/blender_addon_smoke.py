"""Smoke test for Sisir for Blender, run INSIDE Blender by tools/build_blender_addon.py --test:

  blender -b --factory-startup --python tools/blender_addon_smoke.py -- <scene.sir> <Sisir.exe> <out dir> <addon dir>

The counterpart of tools/max_plugin_smoke.py. BLENDER_USER_SCRIPTS points at a scratch folder whose
addons/ holds the copy under test, so an add-on installed from Sisir's Plugins menu can never be
the one checked. Imports the scene without the dialog and checks the result against the bake:
object counts, strand counts, materials, Display % / Render % / Width Scale, a Cycles render,
save/reload with identical hair, export_abc (and an exactness check against the bake), Export
Guides (PLY), cancel, and undo. Results go to <out dir>/smoke.log, one PASS/FAIL line each, then
RESULT OK or RESULT FAIL. <out dir>/blender_cm.abc is the export tools/abc_export_parity_tests.cpp
compares with Sisir's own.
"""
import os
import sys
import time
import traceback

import addon_utils
import bpy
import numpy as np

sir, sisir_exe, out_dir, addon_dir = [os.path.abspath(a) for a in sys.argv[sys.argv.index("--") + 1:][:4]]
os.makedirs(out_dir, exist_ok=True)
log = open(os.path.join(out_dir, "smoke.log"), "w", encoding="utf-8")
failures = []
tools = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, tools)
import sirbake_reader  # noqa: E402  (tools/, the independent reader)


def say(text):
    log.write(text + "\n")
    log.flush()
    print(text)


def check(cond, what):
    say(("PASS " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


def hair_objects():
    return [o for o in bpy.data.objects if o.type == "CURVES" and o.sisir_hair.is_hair]


def visible(entries):
    # As Sisir showed them: a layer in a hidden Layers panel folder is hidden too.
    return [e for e in entries if e.get("visible", True) and all(f.get("visible", True) for f in e.get("folders") or [])]


def collection_tree(suffix):
    """Every collection at or under the import's <scene><suffix> collection (folders nest in it)."""
    out = set()

    def walk(c):
        out.add(c)
        for child in c.children:
            walk(child)
    for c in bpy.data.collections:
        if c.name.endswith(suffix):
            walk(c)
    return out


def folder_chain(obj, suffix):
    """The collection names from <scene><suffix> down to obj's collection, outermost first."""
    for c in obj.users_collection:
        chain = []
        node = c
        while node is not None and not node.name.endswith(suffix):
            chain.insert(0, node)
            node = next((p for p in bpy.data.collections if node.name in p.children), None)
        if node is not None:
            return chain
    return None


def layer_collection(root, collection):
    if root.collection == collection:
        return root
    for child in root.children:
        found = layer_collection(child, collection)
        if found is not None:
            return found
    return None


def bounds(objs):
    pts = []
    for o in objs:
        m = np.array(o.matrix_world)
        for c in o.bound_box:
            pts.append((m @ np.array(list(c) + [1.0]))[:3])
    pts = np.array(pts)
    return pts.min(axis=0), pts.max(axis=0)


def overlap(a, b):
    return bool(np.all(a[0] <= b[1]) and np.all(b[0] <= a[1]))


def evaluated_strands(obj):
    dg = bpy.context.evaluated_depsgraph_get()
    return len(obj.evaluated_get(dg).data.curves)


def clear_scene():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    for c in list(bpy.data.collections):
        bpy.data.collections.remove(c)
    bpy.data.orphans_purge(do_recursive=True)


def select_only(objs):
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    if objs:
        bpy.context.view_layer.objects.active = objs[0]


def main():
    mod = addon_utils.enable("sisir_for_blender", default_set=False)
    check(mod is not None, "the add-on loads")
    import sisir_for_blender.api as sisir
    check(os.path.normcase(sisir.plugin_path()) == os.path.normcase(os.path.join(addon_dir, "sisir_for_blender")),
          "the add-on under test is the one loaded (%s)" % sisir.plugin_path())

    clear_scene()
    t = time.time()
    # Textures into the scratch folder, never the default next to the (unsaved) .blend: Documents.
    # Every kind the checks below count, stated: the user's remembered import settings (cards=0, say)
    # would otherwise decide what this test sees.
    textures = "mesh=1;guides=1;hair=1;cards=1;textureFolder=" + os.path.join(out_dir, "textures")
    root_name = sisir.import_file(sir, options=textures, no_dialog=True, sisir=sisir_exe)
    say("import took %.2fs" % (time.time() - t))
    root = bpy.data.objects.get(root_name)
    check(root is not None, "import_file returns the root")

    # The same bake, read independently.
    from sisir_for_blender.bridge import find_bridge
    from sisir_for_blender import bridge as _bridge
    bridge_exe, _ = find_bridge(sisir_exe)
    result = [l for l in _bridge.run(bridge_exe, ["bake", sir, "--assets", os.path.join(out_dir, "assets"), "--sisir", sisir_exe])
              if l.startswith("RESULT\t")]
    meta, chunks = sirbake_reader.load(result[0].split("\t")[1])
    sets = visible(meta.get("hairSets", []))
    hair = hair_objects()
    check(len(hair) == len(sets), "one hair object per visible set (%d)" % len(sets))
    strands = sum(len(o.data.curves) for o in hair)
    check(strands == sum(s["strands"] for s in sets), "strand count matches the bake (%d)" % strands)
    points = sum(len(o.data.points) for o in hair)
    check(points == sum(s["points"] for s in sets), "point count matches the bake (%d)" % points)
    layers = [l for l in visible(meta.get("guideLayers", [])) if l.get("curves", 0) > 0]
    guides = [o for o in bpy.data.objects if o.type == "CURVE" and "sisir_layer_name" in o]
    check(sum(len(o.data.splines) for o in guides) == sum(l["curves"] for l in layers), "one spline per guide")
    check(len(guides) == len(layers), "one curve object per visible guide layer (%d)" % len(layers))
    card_layers = [l for l in visible(meta.get("cardLayers", [])) if (l.get("facesSmooth") or l.get("facesRaw"))]
    card_collections = collection_tree("_Cards")
    cards = [o for o in bpy.data.objects if o.type == "MESH" and any(c in card_collections for c in o.users_collection)]
    check(len(cards) == len(card_layers), "one card object per visible Cards layer (%d)" % len(card_layers))
    meshes = [o for o in bpy.data.objects if o.type == "MESH" and any(c.name.endswith("_Mesh") for c in o.users_collection)]
    if meta.get("mesh", {}).get("present"):
        check(len(meshes) > 0, "the mesh is imported")
        check(overlap(bounds(hair), bounds(meshes)), "hair and mesh bounds overlap")
        check(all(o.data.materials and o.data.materials[0] for o in meshes), "the mesh has its material")
        if meta.get("meshMaterial", {}).get("albedoTexture"):
            mat = meshes[0].data.materials[0]
            check(any(n.bl_idname == "ShaderNodeTexImage" and n.image for n in mat.node_tree.nodes), "the mesh material carries its colour map")
    check(all(o.data.materials and o.data.materials[0] for o in cards), "every card has its material")
    hm = hair[0].data.materials[0] if hair[0].data.materials else None
    check(hm is not None and any(n.bl_idname == "ShaderNodeBsdfHairPrincipled" for n in hm.node_tree.nodes) and
          any(n.bl_idname == "ShaderNodeAttribute" and n.attribute_name == "sisir_albedo" for n in hm.node_tree.nodes),
          "hair wears a Principled Hair BSDF coloured by sisir_albedo")
    check(all(o.parent == root for o in hair + guides + cards + meshes), "everything hangs under the root")
    # Layers panel folders: each layer's object sits in nested collections named after them, and a
    # hidden folder's collection is hidden (the layer keeps its own eye).
    folder_ok = True
    for entry, objs, suffix in [(l, guides, "_Guides") for l in layers] + [(l, cards, "_Cards") for l in card_layers]:
        obj = next((o for o in objs if o.get("sisir_layer_name", o.name) == entry["name"] or o.name.startswith(entry["name"].replace(" ", "_"))), None)
        if obj is None:
            continue
        chain = folder_chain(obj, suffix) or []
        names = [c.name.split(".")[0] for c in chain]
        want = [f["name"] for f in entry.get("folders") or []]
        if names != want:
            folder_ok = False
            say("  %s: collections %s, folders %s" % (entry["name"], names, want))
        for c, f in zip(chain, entry.get("folders") or []):
            lc = layer_collection(bpy.context.view_layer.layer_collection, c)
            if lc is not None and lc.hide_viewport == bool(f.get("visible", True)):
                folder_ok = False
                say("  folder %s: hidden %s, eye %s" % (f["name"], lc.hide_viewport, f.get("visible", True)))
    check(folder_ok, "layers sit in their Layers panel folders, hidden folders hidden")

    # The panel's controls, through the modifier.
    big = max(hair, key=lambda o: len(o.data.curves))
    n = len(big.data.curves)
    rnd = np.empty(n, dtype=np.float32)
    big.data.attributes["strand_random"].data.foreach_get("value", rnd)
    big.sisir_hair.display_percent = 50.0
    check(evaluated_strands(big) == int((rnd < 0.5).sum()), "Display %% 50 keeps the strands below 0.5 (%d of %d)" % (evaluated_strands(big), n))
    big.sisir_hair.display_percent = 100.0
    check(evaluated_strands(big) == n, "Display % 100 draws every strand")
    from sisir_for_blender.hair import modifier_input
    big.sisir_hair.render_percent = 25.0
    check(abs(modifier_input(big.modifiers["Sisir Hair"], "Render %") - 25.0) < 1e-6, "Render % reaches the modifier")
    big.sisir_hair.render_percent = 100.0
    dg = bpy.context.evaluated_depsgraph_get()
    r1 = np.empty(len(big.data.points), dtype=np.float32)
    big.evaluated_get(dg).data.attributes["radius"].data.foreach_get("value", r1)
    big.sisir_hair.width_scale = 2.0
    dg = bpy.context.evaluated_depsgraph_get()
    r2 = np.empty(len(big.data.points), dtype=np.float32)
    big.evaluated_get(dg).data.attributes["radius"].data.foreach_get("value", r2)
    check(np.allclose(r2, r1 * 2.0, rtol=1e-5), "Width Scale 2 doubles every radius")
    big.sisir_hair.width_scale = 1.0
    big.sisir_hair.color_mode = "SET"
    check(big.sisir_hair.color_mode_index == 2.0, "Color Mode reaches the material's switch")
    big.sisir_hair.color_mode = "AUTO"

    # A tiny Cycles render of the hair alone.
    scene = bpy.context.scene
    for o in bpy.data.objects:
        o.hide_render = o not in hair
    lo, hi = bounds(hair)
    centre = (lo + hi) / 2
    cam = bpy.data.objects.new("SmokeCam", bpy.data.cameras.new("SmokeCam"))
    scene.collection.objects.link(cam)
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = float(max(hi - lo)) * 1.1
    cam.location = (centre[0], lo[1] - float(max(hi - lo)) * 2, centre[2])
    cam.rotation_euler = (np.pi / 2, 0, 0)
    cam.data.clip_end = float(max(hi - lo)) * 10
    scene.camera = cam
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 4
    scene.cycles.device = "CPU"
    scene.render.film_transparent = True
    scene.render.resolution_x = scene.render.resolution_y = 64
    scene.render.filepath = os.path.join(out_dir, "render.png")
    bpy.ops.render.render(write_still=True)
    img = bpy.data.images.load(scene.render.filepath)
    alpha = np.array(img.pixels[:]).reshape(-1, 4)[:, 3]
    bpy.data.images.remove(img)
    check((alpha > 0.5).mean() > 0.02, "Cycles renders the hair (%.0f%% coverage)" % (100 * (alpha > 0.5).mean()))
    bpy.data.objects.remove(cam)
    for o in bpy.data.objects:
        o.hide_render = False

    # Save, reload: the hair comes back identical.
    before = {o.name: sisir.hair_stats(o) for o in hair_objects()}
    blend = os.path.join(out_dir, "smoke.blend")
    bpy.ops.wm.save_as_mainfile(filepath=blend)
    bpy.ops.wm.open_mainfile(filepath=blend)
    after = {o.name: sisir.hair_stats(o) for o in hair_objects()}
    check(before == after and len(after) == len(sets), "save/reload gives identical hair")
    big = bpy.data.objects[max(after, key=lambda k: int(after[k].split(";")[0].split("=")[1]))]
    check(big.sisir_hair.is_hair and big.modifiers.get("Sisir Hair") is not None, "the hair object survives reloading")

    # Export, from a Real World import (Sisir's metres as Blender units here).
    clear_scene()
    root_name = sisir.import_file(sir, options="scale=realWorld;" + textures, no_dialog=True, sisir=sisir_exe)
    root = bpy.data.objects[root_name]
    hair = hair_objects()
    select_only([])
    check(sisir.export_abc_count() == 0, "export_abc_count is 0 with nothing selected")
    try:
        sisir.export_abc(os.path.join(out_dir, "none.abc"), no_dialog=True)
        check(False, "export_abc refuses an empty selection")
    except sisir.SisirError:
        check(not os.path.exists(os.path.join(out_dir, "none.abc")), "export_abc refuses an empty selection")
    select_only([root])
    check(sisir.export_abc_count() == len(sets), "the root selected exports every visible set")
    full = os.path.join(out_dir, "blender_cm.abc")
    every = "units=cm;rootUv=1;thickness=1;clumpId=1;color=1;roughness=1;ao=1;unreal=0"
    check(sisir.export_abc(full, options=every, no_dialog=True) and os.path.isfile(full), "export_abc writes every attribute")
    one = min(hair, key=lambda o: len(o.data.curves))
    select_only([one])
    check(sisir.export_abc_count() == 1, "one hair selected exports one")
    small = os.path.join(out_dir, "one.abc")
    sisir.export_abc(small, options=every, no_dialog=True)
    check(os.path.getsize(small) < os.path.getsize(full), "one set makes a smaller file")
    try:
        sisir.export_abc(os.path.join(out_dir, "bad.abc"), options="units=furlongs", no_dialog=True)
        check(False, "units=furlongs is rejected")
    except sisir.SisirError:
        check(True, "units=furlongs is rejected")

    # Exactness: in metres, the exported strands are the bake's, through Blender's own Alembic reader.
    select_only([root])
    metres = os.path.join(out_dir, "blender_m.abc")
    sisir.export_abc(metres, options="units=m;rootUv=1;thickness=1;clumpId=1;color=0;roughness=0;ao=0;unreal=0", no_dialog=True)
    ours = {o.sisir_hair.hair_set: o for o in hair}
    before_import = set(bpy.data.objects)
    bpy.ops.wm.alembic_import(filepath=metres, as_background_job=False)
    imported = [o for o in bpy.data.objects if o not in before_import and o.type == "CURVES"]
    worst = 0.0
    matched = 0
    for o in imported:
        name = o.name.split(".")[0]
        src = next((h for k, h in ours.items() if name in (k, k.replace(" ", "_"))), None)
        if src is None:
            continue
        a = np.empty(len(o.data.points) * 3, dtype=np.float32)
        o.data.attributes["position"].data.foreach_get("vector", a)
        m = np.array(o.matrix_world)
        a = a.reshape(-1, 3) @ m[:3, :3].T + m[:3, 3]
        b = np.empty(len(src.data.points) * 3, dtype=np.float32)
        src.data.attributes["position"].data.foreach_get("vector", b)
        if a.shape == b.reshape(-1, 3).shape:
            worst = max(worst, float(np.abs(a - b.reshape(-1, 3)).max()))
            matched += 1
    check(matched == len(sets) and worst < 1e-5, "the ABC holds the bake's strands exactly (%d sets, worst %.2g m)" % (matched, worst))
    for o in imported:
        bpy.data.objects.remove(o)

    # Export Guides (PLY), from the root.
    ply = os.path.join(out_dir, "guides.ply")
    select_only([root])
    sisir.export_guides_ply(ply, unit="cm")
    text = open(ply, encoding="utf-8").read()
    guides = [o for o in bpy.data.objects if o.type == "CURVE" and "sisir_layer_name" in o]
    header = text.split("end_header", 1)[0]
    vertices = int(header.split("element vertex ")[1].split()[0])
    check(vertices == sum(sum(len(s.points) for s in o.data.splines) for o in guides), "the PLY holds every guide point")
    check(all('"%s"' % o["sisir_layer_name"] for o in guides) and
          all(('"%s"' % o["sisir_layer_name"]) in header for o in guides), "PLY layers are the guide layers")
    check("comment sisir_unit cm\n" in header, "the PLY is in the unit asked for")

    # Undo: one step removes the whole import.
    count = len(bpy.data.objects)
    if bpy.ops.ed.undo.poll():
        bpy.ops.ed.undo()
        check(len(bpy.data.objects) < count and not hair_objects(), "undo removes the import")
        bpy.ops.ed.redo()
        check(len(bpy.data.objects) == count, "redo restores it")
    else:
        say("SKIP undo: no undo stack in background mode")

    say("RESULT " + ("OK" if not failures else "FAIL (%d)" % len(failures)))


try:
    main()
except Exception:
    say("FAIL exception:\n" + traceback.format_exc())
    say("RESULT FAIL")
log.close()
