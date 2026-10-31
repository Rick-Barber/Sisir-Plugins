"""Smoke test for Sisir for 3ds Max, run INSIDE 3ds Max (its Python, pymxs) by
tools/build_max_plugins.py --test:

  3dsmax.exe -silent -mxs "python.ExecuteFile @\"tools/max_plugin_smoke.py\""
  with SISIR_MAX_SMOKE=<bundle dir>|<scene.sir>|<Sisir.exe>|<out dir>

The counterpart of tools/maya_plugin_smoke.py. Loads the plug-in from a built bundle (refusing to run
if another SisirForMax.dlo is already loaded, e.g. one installed from Sisir's Plugins menu), imports
the scene without the dialog and checks the result against the bake: node counts, strand counts,
save/reload with identical hair, sisir.exportAbc, Export Guides (PLY), undo/redo, and (with MAXtoA)
the Arnold translation in an exported .ass. Results go to <out dir>/smoke.log, one PASS/FAIL line
each, then DONE; 3ds Max quits at the end.
"""
import os
import re
import sys
import traceback

from pymxs import runtime as rt

bundle_dir, sir, sisir_exe, out_dir = [os.path.abspath(a) for a in os.environ["SISIR_MAX_SMOKE"].split("|")]
os.makedirs(out_dir, exist_ok=True)
log = open(os.path.join(out_dir, "smoke.log"), "w", encoding="utf-8")
failures = []


def say(text):
    log.write(text + "\n")
    log.flush()
    print(text)


def check(cond, what):
    say(("PASS " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


def all_nodes():
    return list(rt.objects)


def hair_nodes():
    return [n for n in all_nodes() if rt.sisir.hairStats(n)]


def stats(node):
    return dict(kv.split("=") for kv in rt.sisir.hairStats(node).split(";"))


def child_named(root, suffix):
    for c in root.children:
        if c.name.endswith(suffix):
            return c
    return None


def descendants(node):
    out = []
    for c in node.children:
        out.append(c)
        out.extend(descendants(c))
    return out


def main():
    version = int(rt.maxVersion()[7])
    plugin = os.path.join(bundle_dir, "Contents", str(version), "SisirForMax.dlo")
    say("3ds Max %d, plug-in %s" % (version, plugin))

    # An installed Sisir for 3ds Max loads itself at startup and would be tested instead of this build.
    if rt.execute("sisir != undefined"):
        loaded = rt.sisir.pluginPath()
        if os.path.normcase(os.path.abspath(loaded)) != os.path.normcase(plugin):
            say("FAIL another Sisir for 3ds Max is already loaded (%s): uninstall it from Sisir's Plugins menu first" % loaded)
            failures.append("other copy loaded")
            return
    else:
        rt.loadDllsFromDir(os.path.dirname(plugin), "*.dlo")
    check(rt.execute("sisir != undefined"), "plug-in loads")
    check(os.path.normcase(rt.sisir.pluginPath()) == os.path.normcase(plugin), "the build under test is the one loaded")
    have_arnold = bool(rt.execute("(try (classOf ai_standard_hair == material) catch (false))"))
    say("  Arnold (MAXtoA): %s" % have_arnold)

    # The bake, for expected counts.
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import subprocess
    import sirbake_reader
    bake_path = os.path.join(out_dir, "expected.sirbake")
    subprocess.run([sisir_exe, "--bake", sir, bake_path, "--assets", os.path.join(out_dir, "expected_assets")], check=True)
    meta, chunks = sirbake_reader.load(bake_path)
    visible_sets = [s for s in meta["hairSets"] if s["visible"]]
    expected_strands = sum(s["strands"] for s in visible_sets)
    visible_guides = [l for l in meta["guideLayers"] if l["visible"]]
    expected_curves = sum(l["curves"] for l in visible_guides)
    visible_cards = [l for l in meta["cardLayers"] if l["visible"]]

    tex = os.path.join(out_dir, "textures").replace("\\", "/")
    # Every option explicit: the import otherwise starts from the user's remembered dialog choices.
    defaults = ("mesh=1;refMesh=1;materials=1;guides=1;hair=1;cards=1;includeHidden=0;scale=matchMesh;"
                "guideDegree=1;displayPercent=100;smoothCards=1;textureFolder=" + tex)

    def do_import(options=defaults):
        return rt.sisir.importFile(sir, options=options, noDialog=True, sisir=sisir_exe)

    rt.resetMaxFile(rt.Name("noPrompt"))
    root_name = do_import()
    root = rt.getNodeByName(root_name) if root_name else None
    check(root is not None, "sisir.importFile returns the root: %s" % root_name)
    if root is None:
        return

    hairs = hair_nodes()
    check(len(hairs) == len(visible_sets), "one hair object per visible set (%d / %d)" % (len(hairs), len(visible_sets)))
    strands = sum(int(stats(h)["strands"]) for h in hairs)
    check(strands == expected_strands, "strand count matches the bake (%d / %d)" % (strands, expected_strands))
    guides = child_named(root, "_Guides")
    curves = sum(rt.numSplines(n) for n in (guides.children if guides else []))
    check(curves == expected_curves, "one spline per guide (%d / %d)" % (curves, expected_curves))
    check(len(guides.children if guides else []) == len(visible_guides), "one spline shape per visible guide layer")
    cards = child_named(root, "_Cards")
    check(len(cards.children if cards else []) == len(visible_cards),
          "one card object per visible Cards layer (%d / %d)" % (len(cards.children if cards else []), len(visible_cards)))
    mesh = child_named(root, "_Mesh")
    check(mesh is not None and len(mesh.children) > 0, "the mesh is imported")
    if hairs and mesh is not None and len(mesh.children):
        hb = (hairs[0].min, hairs[0].max)
        m = mesh.children[0]
        mb = (m.min, m.max)
        say("  hair bbox %s %s, mesh bbox %s %s" % (hb[0], hb[1], mb[0], mb[1]))
        overlap = all(getattr(hb[0], a) < getattr(mb[1], a) + (getattr(mb[1], a) - getattr(mb[0], a)) and
                      getattr(hb[1], a) > getattr(mb[0], a) - (getattr(mb[1], a) - getattr(mb[0], a)) for a in "xyz")
        check(overlap, "hair sits on the mesh (bounding boxes overlap)")
        # Sisir is Y-up, Max Z-up: a head stands along Z.
        mz = mb[1].z - mb[0].z
        my = mb[1].y - mb[0].y
        say("  mesh extent y %.2f z %.2f" % (my, mz))

    # Every surface gets its material; a textured one shows its colour map in the viewport.
    mesh_nodes = list(mesh.children) if mesh is not None else []
    check(bool(mesh_nodes) and all(n.material is not None for n in mesh_nodes), "the mesh has its material")
    if meta["meshMaterial"].get("albedoTexture") and mesh_nodes and mesh_nodes[0].material is not None:
        mat = mesh_nodes[0].material
        slot = "base_color_shader" if rt.isProperty(mat, "base_color_shader") else "base_color_map"
        check(rt.getProperty(mat, slot) is not None, "the mesh material carries its colour map")
    card_nodes = list(cards.children) if cards is not None else []
    check(all(n.material is not None for n in card_nodes), "every card object has its material")

    if have_arnold:
        check(all(str(rt.classOf(h.material)) == "ai_standard_hair" for h in hairs), "hair wears ai_standard_hair")
        rt.renderers.current = rt.Arnold()
        ass = os.path.join(out_dir, "scene.ass")
        if os.path.exists(ass):
            os.remove(ass)
        rt.exportFile(ass, rt.Name("noPrompt"))
        text = open(ass, "r", errors="replace").read() if os.path.isfile(ass) else ""
        check("curves" in text, "the .ass holds Arnold curves")
        check("sisir_albedo" in text, "curves carry sisir_albedo")
        check("ribbon" in text, "hair renders as camera-facing ribbons")
        check("standard_hair" in text, "hair uses standard_hair")
    else:
        say("  Arnold (MAXtoA) not available: checking the fallback")
        check(all(h.material is None for h in hairs), "without Arnold the hair gets no material")

    # Save and reload: the hair must come back from the file, exactly.
    before = sorted((h.name, rt.sisir.hairStats(h)) for h in hairs)
    max_path = os.path.join(out_dir, "roundtrip.max")
    rt.saveMaxFile(max_path, quiet=True)
    rt.resetMaxFile(rt.Name("noPrompt"))
    check(not hair_nodes(), "reset clears the scene")
    rt.loadMaxFile(max_path, quiet=True)
    after = sorted((h.name, rt.sisir.hairStats(h)) for h in hair_nodes())
    check(after == before, "roundtrip.max reloads every strand, bit for bit (%d sets)" % len(after))

    # Alembic export works on the selection only. The root selected holds every visible hair set:
    # that export is what tools/abc_export_parity_tests.cpp compares with Sisir's own (scale=realWorld).
    rt.resetMaxFile(rt.Name("noPrompt"))
    root = rt.getNodeByName(do_import(defaults.replace("scale=matchMesh", "scale=realWorld")))
    abc_all = os.path.join(out_dir, "max_export.abc")
    abc_one = os.path.join(out_dir, "max_export_one.abc")
    abc_none = os.path.join(out_dir, "max_export_none.abc")
    for f in (abc_all, abc_one, abc_none):
        if os.path.exists(f):
            os.remove(f)
    rt.clearSelection()
    check(rt.sisir.exportAbcCount() == 0, "exportAbcCount finds no hair with nothing selected")
    check(not rt.sisir.exportAbc(abc_none, noDialog=True) and not os.path.exists(abc_none), "exportAbc exports nothing with nothing selected")
    rt.select(root)
    check(rt.sisir.exportAbcCount() == len(visible_sets), "the root holds every visible hair set (%d)" % len(visible_sets))
    written = rt.sisir.exportAbc(abc_all, options="units=cm;rootUv=1;thickness=1;clumpId=1;color=1;roughness=1;ao=1;unreal=0", noDialog=True)
    check(os.path.isfile(abc_all) and os.path.getsize(abc_all) > 0, "exportAbc writes the selected group's hair (%s)" % written)
    rt.select(hair_nodes()[0])
    check(rt.sisir.exportAbcCount() == 1, "one selected hair object is one hair set")
    rt.sisir.exportAbc(abc_one, noDialog=True)
    check(os.path.isfile(abc_one) and os.path.getsize(abc_one) < os.path.getsize(abc_all), "exportAbc with one hair set selected writes only that set")
    check(not rt.sisir.exportAbc(abc_one, options="units=furlongs", noDialog=True), "exportAbc rejects a bad option")
    say("MAX_ABC " + abc_all)

    # Export Guides (PLY): the Sisir menu runs this script on the selection.
    import runpy
    exporter = runpy.run_path(os.path.join(bundle_dir, "Contents", "scripts", "3DSMax_to_PLY.py"), run_name="sisir_guides_ply")

    def ply_curves(path):
        ids, layers, header, props = set(), [], True, []
        with open(path) as f:
            for line in f:
                if header:
                    if line.startswith("comment layer"):
                        layers.append(line.split('"')[1])
                    elif line.startswith("property"):
                        props.append(line.split()[-1])
                    elif line.startswith("end_header"):
                        header = False
                    continue
                ids.add(int(line.split()[props.index("curve_id")]))
        return len(ids), layers

    ply_all = os.path.join(out_dir, "guides_all.ply")
    rt.select(root)
    exporter["export_sisir_guides"](path=ply_all)
    n, layers = ply_curves(ply_all) if os.path.isfile(ply_all) else (0, [])
    check(n == expected_curves, "the root exports every visible guide (%d / %d)" % (n, expected_curves))
    check(sorted(layers) == sorted(l["name"] for l in visible_guides), "one guide layer per imported shape, named after it (%s)" % layers)

    # Import undo / redo.
    rt.resetMaxFile(rt.Name("noPrompt"))
    do_import()
    n = len(hair_nodes())
    total = len(all_nodes())
    rt.execute("max undo")
    check(len(all_nodes()) == 0, "undo removes the whole import (%d left of %d)" % (len(all_nodes()), total))
    rt.execute("max redo")
    check(len(hair_nodes()) == n and len(all_nodes()) == total, "redo brings it back (%d / %d nodes)" % (len(all_nodes()), total))


try:
    main()
except Exception:
    say("FAIL exception:\n" + traceback.format_exc())
    failures.append("exception")
finally:
    say("RESULT %s (%d failures)" % ("OK" if not failures else "FAILED", len(failures)))
    say("DONE")
    log.close()
    rt.quitMax(rt.Name("noPrompt"))
