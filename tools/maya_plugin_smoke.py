"""Headless smoke test for Sisir for Maya. Run with mayapy:

  "C:/Program Files/Autodesk/Maya2025/bin/mayapy.exe" tools/maya_plugin_smoke.py <module dir> <scene.sir> <Sisir.exe> <out dir>

With MAYA_APP_DIR set to an empty folder, so a Sisir for Maya installed in the user's Maya
profile does not load itself in place of the build under test.

Loads the plug-in from a built module, imports the scene without the dialog, and checks the
result against the bake: node counts, strand counts, scene save/reopen, sisirExportAbc, undo/redo,
and (when MtoA is present) the Arnold translator's output in an exported .ass.
"""
import os
import sys

module_dir, sir, sisir_exe, out_dir = [os.path.abspath(a) for a in sys.argv[1:5]]
version = None

import maya.standalone
maya.standalone.initialize(name="python")
import maya.cmds as cmds
import maya.mel as mel

version = cmds.about(majorVersion=True)
plugin = os.path.join(module_dir, "plug-ins", version, "SisirForMaya.mll")
ext = os.path.join(module_dir, "extensions", version)
os.environ["MTOA_EXTENSIONS_PATH"] = ext + os.pathsep + os.environ.get("MTOA_EXTENSIONS_PATH", "")
os.environ["MAYA_SCRIPT_PATH"] = os.path.join(module_dir, "scripts") + os.pathsep + os.environ.get("MAYA_SCRIPT_PATH", "")

failures = []
def check(cond, what):
    print(("PASS " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)

# An installed Sisir for Maya (Sisir > Plugins) loads itself at startup and would be tested instead
# of this build: run with MAYA_APP_DIR set to an empty folder (tools/build_maya_plugins.py does).
if cmds.pluginInfo("SisirForMaya", q=True, loaded=True):
    loaded_from = cmds.pluginInfo("SisirForMaya", q=True, path=True)
    if os.path.normcase(os.path.abspath(loaded_from)) != os.path.normcase(plugin):
        print("FAIL another Sisir for Maya is already loaded (%s): set MAYA_APP_DIR to an empty folder" % loaded_from)
        os._exit(1)
cmds.loadPlugin(plugin)
check(cmds.pluginInfo("SisirForMaya", q=True, loaded=True), "plug-in loads")
have_arnold = False
try:
    cmds.loadPlugin("mtoa", quiet=True)
    have_arnold = cmds.pluginInfo("mtoa", q=True, loaded=True)
except Exception as e:
    print("mtoa unavailable:", e)

# The bake, for expected counts.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import subprocess, sirbake_reader
bake_path = os.path.join(out_dir, "expected.sirbake")
subprocess.run([sisir_exe, "--bake", sir, bake_path, "--assets", os.path.join(out_dir, "expected_assets")], check=True)
meta, chunks = sirbake_reader.load(bake_path)
visible_sets = [s for s in meta["hairSets"] if s["visible"]]
expected_strands = sum(s["strands"] for s in visible_sets)
# As Sisir showed them: a layer in a hidden Layers panel folder is hidden too (MetaFolders.h).
shown = lambda l: l["visible"] and all(f.get("visible", True) for f in l.get("folders") or [])
visible_guides = [l for l in meta["guideLayers"] if shown(l)]
expected_curves = sum(l["curves"] for l in visible_guides)
visible_cards = [l for l in meta["cardLayers"] if shown(l)]

tex = os.path.join(out_dir, "textures")
# Every option explicit: sisirImport otherwise starts from the user's remembered dialog choices.
DEFAULTS = ("mesh=1;refMesh=1;materials=1;guides=1;hair=1;cards=1;includeHidden=0;scale=matchMesh;"
            "guideDegree=1;displayPercent=100;smoothCards=1;")
root = cmds.sisirImport(file=sir, noDialog=True, sisir=sisir_exe, options=DEFAULTS + "textureFolder=" + tex.replace("\\", "/"))
check(bool(root) and cmds.objExists(root), "sisirImport returns the root group: %s" % root)

shapes = cmds.ls(type="sisirHairShape", long=True) or []
check(len(shapes) == len(visible_sets), "one hair shape per visible set (%d / %d)" % (len(shapes), len(visible_sets)))
strands = sum(cmds.getAttr(s + ".strandCount") for s in shapes)
check(strands == expected_strands, "strand count matches the bake (%d / %d)" % (strands, expected_strands))
curves = cmds.ls(type="nurbsCurve") or []
check(len(curves) == expected_curves, "one guide curve per guide (%d / %d)" % (len(curves), expected_curves))
# Anywhere under <scene>_Cards: a layer in folders sits in nested groups named after them.
card_shapes = cmds.listRelatives(cmds.ls(root + "|*_Cards") or [], allDescendents=True, type="mesh", fullPath=True) or []
card_meshes = sorted(set(cmds.listRelatives(card_shapes, parent=True, fullPath=True) or []))
check(len(card_meshes) == len(visible_cards), "one card mesh per visible Cards layer (%d / %d)" % (len(card_meshes), len(visible_cards)))

# Layers panel folders: each layer's group sits in groups named after its folders, a hidden
# folder's group is hidden, and the layer keeps its own eye.
import re as _re
_legal = lambda name: _re.sub(r"\W", "_", name)
def _folder_chain(node, top):
    chain = []
    parent = cmds.listRelatives(node, parent=True, fullPath=True)
    while parent and parent[0] != top:
        chain.insert(0, parent[0])
        parent = cmds.listRelatives(parent[0], parent=True, fullPath=True)
    return chain
folder_ok = True
guide_groups = [g for g in (cmds.ls(root + "|*_Guides", dag=True, type="transform", long=True) or [])
                if cmds.attributeQuery("sisirLayerName", node=g, exists=True)]
for entry in visible_guides:
    group = next((g for g in guide_groups if cmds.getAttr(g + ".sisirLayerName") == entry["name"]), None)
    if group is None:
        continue
    chain = _folder_chain(group, (cmds.ls(root + "|*_Guides", long=True) or [""])[0])
    names = [c.split("|")[-1] for c in chain]
    want = [_legal(f["name"]) for f in entry.get("folders") or []]
    if [n.rstrip("0123456789") for n in names] != [w.rstrip("0123456789") for w in want]:
        folder_ok = False
        print("  %s: groups %s, folders %s" % (entry["name"], names, want))
    for c, f in zip(chain, entry.get("folders") or []):
        if cmds.getAttr(c + ".visibility") != bool(f.get("visible", True)):
            folder_ok = False
            print("  folder %s: visibility %s" % (f["name"], cmds.getAttr(c + ".visibility")))
for entry in visible_cards:
    want = [_legal(f["name"]) for f in entry.get("folders") or []]
    mesh = next((m for m in card_meshes if m.split("|")[-1] == _legal(entry["name"])), None)
    if mesh is None:
        continue
    names = [c.split("|")[-1] for c in _folder_chain(mesh, (cmds.ls(root + "|*_Cards", long=True) or [""])[0])]
    if [n.rstrip("0123456789") for n in names] != [w.rstrip("0123456789") for w in want]:
        folder_ok = False
        print("  %s: groups %s, folders %s" % (entry["name"], names, want))
check(folder_ok, "layers sit in their Layers panel folders, hidden folders hidden")
mesh_group = cmds.ls(root + "|*_Mesh")
check(bool(mesh_group) and bool(cmds.listRelatives(mesh_group[0], allDescendents=True, type="mesh")), "the mesh is imported")
if shapes:
    bb = cmds.exactWorldBoundingBox(shapes[0])
    print("  hair bbox", ["%.2f" % v for v in bb])
    mb = cmds.exactWorldBoundingBox(mesh_group[0]) if mesh_group else None
    print("  mesh bbox", ["%.2f" % v for v in mb] if mb else None)
    if mb:
        overlap = all(bb[i] < mb[i + 3] + (mb[i + 3] - mb[i]) and bb[i + 3] > mb[i] - (mb[i + 3] - mb[i]) for i in range(3))
        check(overlap, "hair sits on the mesh (bounding boxes overlap)")

if have_arnold:
    sgs = cmds.listConnections(shapes[0] + ".instObjGroups", type="shadingEngine") or []
    check(bool(sgs), "hair has a shading group")
    ass = os.path.join(out_dir, "scene.ass")
    cmds.arnoldExportAss(f=ass, mask=255, lightLinks=False, shadowLinks=False)
    text = open(ass, "r", errors="replace").read() if os.path.isfile(ass) else ""
    check(" curves" in text or "\ncurves" in text, "the .ass holds Arnold curves")
    check("sisir_albedo" in text, "curves carry sisir_albedo")
    check("ribbon" in text, "hair renders as camera-facing ribbons")
    check("aiStandardHair" in text or "standard_hair" in text, "hair uses aiStandardHair")
else:
    # No Arnold: the hair still imports and draws, without a shader; surfaces use Maya's own.
    print("  Arnold (MtoA) not available: checking the fallback")
    check(not (cmds.listConnections(shapes[0] + ".instObjGroups", type="shadingEngine") or []), "without Arnold the hair gets no shader")
    check(bool(cmds.ls(type="standardSurface")), "without Arnold surfaces use standardSurface")

# Save and reopen: the hair must come back from the file.
mb_path = os.path.join(out_dir, "roundtrip.mb")
cmds.file(rename=mb_path)
cmds.file(save=True, type="mayaBinary", force=True)
ma_path = os.path.join(out_dir, "roundtrip.ma")
cmds.file(rename=ma_path)
cmds.file(save=True, type="mayaAscii", force=True)
for path in (mb_path, ma_path):
    cmds.file(path, open=True, force=True)
    back = cmds.ls(type="sisirHairShape", long=True) or []
    back_strands = sum(cmds.getAttr(s + ".strandCount") for s in back)
    check(back_strands == expected_strands, "%s reopens with every strand (%d)" % (os.path.basename(path), back_strands))

# Alembic export works on the selection only. The import's root group selected holds every
# visible hair set, nested two levels down: that export is what tools/abc_export_parity_tests.cpp
# compares with Sisir's own (it needs scale=realWorld).
cmds.file(new=True, force=True)
root = cmds.sisirImport(file=sir, noDialog=True, sisir=sisir_exe, options=DEFAULTS.replace("scale=matchMesh", "scale=realWorld") + "textureFolder=" + tex.replace("\\", "/"))
abc_all = os.path.join(out_dir, "maya_export.abc")
abc_one = os.path.join(out_dir, "maya_export_one.abc")
abc_none = os.path.join(out_dir, "maya_export_none.abc")
for f in (abc_all, abc_one, abc_none):
    if os.path.exists(f):
        os.remove(f)
cmds.select(clear=True)
check(cmds.sisirExportAbc(check=True) == 0, "sisirExportAbc -check finds no hair with nothing selected")
try:
    cmds.sisirExportAbc(file=abc_none, noDialog=True)
    exported_unselected = True
except RuntimeError:
    exported_unselected = False
check(not exported_unselected and not os.path.exists(abc_none), "sisirExportAbc exports nothing with nothing selected")
cmds.select(root)
check(cmds.sisirExportAbc(check=True) == len(visible_sets), "the root group holds every visible hair set (%d)" % len(visible_sets))
written = cmds.sisirExportAbc(file=abc_all, noDialog=True, options="units=cm;rootUv=1;thickness=1;clumpId=1;color=1;roughness=1;ao=1;unreal=0")
check(os.path.isfile(abc_all) and os.path.getsize(abc_all) > 0, "sisirExportAbc writes the selected group's hair (%s)" % written)
shapes = cmds.ls(type="sisirHairShape", long=True)
cmds.select(cmds.listRelatives(shapes[0], parent=True, fullPath=True)[0])
check(cmds.sisirExportAbc(check=True) == 1, "one selected hair transform is one hair set")
cmds.sisirExportAbc(file=abc_one, noDialog=True)
check(os.path.isfile(abc_one) and os.path.getsize(abc_one) < os.path.getsize(abc_all), "sisirExportAbc with one hair set selected writes only that set")
try:
    cmds.sisirExportAbc(file=abc_one, noDialog=True, options="units=furlongs")
    check(False, "sisirExportAbc rejects a bad option")
except RuntimeError:
    check(True, "sisirExportAbc rejects a bad option")
print("MAYA_ABC", abc_all)

# Export Guides (PLY): the Sisir menu runs this script on the selection.
sys.path.insert(0, os.path.join(module_dir, "scripts"))
import Maya_curves_to_PLY


def ply_curves(path):
    curves, layers, header = set(), [], True
    with open(path) as f:
        props = []
        for line in f:
            if header:
                if line.startswith("comment layer"):
                    layers.append(line.split('"')[1])
                elif line.startswith("property"):
                    props.append(line.split()[-1])
                elif line.startswith("end_header"):
                    header = False
                continue
            curves.add(int(line.split()[props.index("curve_id")]))
    return len(curves), layers


ply_all = os.path.join(out_dir, "guides_all.ply")
ply_one = os.path.join(out_dir, "guides_one.ply")
cmds.select(clear=True)
check(Maya_curves_to_PLY.export_selected_curves_to_ply(path=os.path.join(out_dir, "guides_none.ply")) is None
      and not os.path.exists(os.path.join(out_dir, "guides_none.ply")), "Export Guides exports nothing with nothing selected")
cmds.select(root)
Maya_curves_to_PLY.export_selected_curves_to_ply(path=ply_all)
n, layers = ply_curves(ply_all)
check(n == expected_curves, "the root group exports every visible guide (%d / %d)" % (n, expected_curves))
import re
maya_name = lambda name: re.sub(r"\W", "_", name)  # as Maya node names spell them
check(sorted(maya_name(l) for l in layers) == sorted(maya_name(l["name"]) for l in visible_guides),
      "one guide layer per layer group, named after it (%s)" % layers)
one_curve = cmds.listRelatives(cmds.ls(type="nurbsCurve", long=True)[0], parent=True, fullPath=True)[0]
cmds.select(one_curve)
Maya_curves_to_PLY.export_selected_curves_to_ply(path=ply_one)
check(ply_curves(ply_one)[0] == 1, "one selected curve exports one guide")

# Import undo/redo.
cmds.file(new=True, force=True)
cmds.sisirImport(file=sir, noDialog=True, sisir=sisir_exe, options=DEFAULTS + "textureFolder=" + tex.replace("\\", "/"))
n = len(cmds.ls(type="sisirHairShape"))
cmds.undo()
check(len(cmds.ls(type="sisirHairShape")) == 0 and len(cmds.ls(type="nurbsCurve")) == 0, "undo removes the whole import")
cmds.redo()
check(len(cmds.ls(type="sisirHairShape")) == n, "redo brings it back")

print("RESULT", "FAIL" if failures else "PASS", len(failures))
maya.standalone.uninitialize()
os._exit(1 if failures else 0)
