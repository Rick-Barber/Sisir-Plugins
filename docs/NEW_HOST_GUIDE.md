# Writing a Sisir plug-in for a new program

This guide is for bringing Sisir scenes into a program the official plug-ins don't cover: Houdini,
LightWave, Cinema 4D, Modo, Unreal, Unity, your studio's pipeline, and so on. Read
[HOW_IT_WORKS.md](HOW_IT_WORKS.md) first. The short version: you run `Sisir.exe --bake`, read the
`.sirbake` it writes, and build the scene in your host.

## Choose a starting point

| Your host has... | Start from | Why |
| --- | --- | --- |
| A C++ SDK and its own Qt (Houdini, Modo, Katana, ...) | **Sisir for Maya** (`plugins/maya`) | Compile `plugins/common` and the shared `src/` files in against the host's Qt and get the real import dialog, bake progress and ABC export for free. 3ds Max (`plugins/max`) shows how to do it when the SDK ships Qt DLLs but no headers. |
| Python, no Qt (or you'd rather not compile) | **Sisir for Blender** (`plugins/blender`) | The add-on is pure Python and drives `SisirBridge.exe` for the dialogs, bake and ABC writer. Copy `bridge.py`, `options.py` and `sirbake.py` almost unchanged and rewrite only `builder.py` (scene building) and the menu. |
| Neither, or no UI at all (batch, a game engine importer) | **The bake alone** | Run `Sisir.exe --bake` yourself and read the file with `SirBakeReader` (C++) or `sirbake.py` / `tools/sirbake_reader.py` (Python). No Sisir code beyond those. |

Houdini, for example: its Python (`hou`) plus `SisirBridge.exe` gets a working importer fastest,
the Blender way. A C++ HDK plug-in with the Qt import dialog is the more native route, the Maya
way.

## The minimum importer

1. **Find Sisir.exe.** Use the order in HOW_IT_WORKS §1. The registry file association is the key
   step; also let the user browse for it, and remember the choice.
2. **Bake.** `Sisir.exe --bake scene.sir <temp>/scene.sirbake --assets <temp>/scene_assets --progress`.
   Show the `PROGRESS` lines, and kill the process if the user cancels. Check the exit code.
3. **Read.** Validate the header, then walk the chunks, checking each CRC. Parse META first.
4. **Mesh.** Load `META.mesh.file` (an OBJ in the assets folder) and its texture.
5. **Guides.** Each `GUID` chunk is one layer of polylines: make a curve group named after
   `guideLayers[i].name`.
6. **Hair.** Each `HAIR` chunk is one hair set: strands as curves with per-point width and colour.
   Hair sets can hold millions of points. Use your host's native hair or curve primitive, and
   consider a display percentage as the official plug-ins do.
7. **Cards.** Each `CARD` chunk is a polygon mesh with normals and UVs. Usually import either the
   raw (flags 0) or the smooth (flags 1) version of a layer, not both. The card textures and alpha
   cutoff are in `cardMaterials[uvId]`.
8. **Units and axes.** Sisir is metres, Y-up. Apply one scale and, for Z-up hosts, a rotation
   (Y-up to Z-up: (x, y, z) to (x, -z, y)), the same way for everything.

Steps 1–3 are already written for you in `Baker` + `SirBakeReader` (C++) and `bridge.py` +
`sirbake.py` (Python).

## Doing it well

The official plug-ins go further. Look at them for:

- **Groups and folders**: `MetaFolders.h` and the `folders` arrays rebuild Sisir's Layers panel
  folders as nested groups.
- **Materials**: `hairMaterials` maps to a physical hair shader (Arnold `standard_hair` in Maya and
  3ds Max, Principled Hair in Blender). See `plugins/maya/src/SceneBuilder.cpp`,
  `plugins/max/src/SceneBuilder.cpp` and `plugins/blender/sisir_for_blender/materials.py`.
- **One root per import**: everything an import creates goes under one group named after the
  scene, so the user can move, hide or delete it in one step.
- **Export back**: an *Export Guides (PLY)* command using `sisir_ply.py` closes the loop with
  Sisir's File > Import Curves (PLY).
- **Tests**: `tools/*_smoke.py` import a real scene in each host and check counts against
  `tools/sirbake_reader.py`. Do the same for your host; it catches unit and axis mistakes at once.

## Before you publish

Read [FORKING.md](FORKING.md). A new host has fewer clashes than a fork of an existing plug-in, but
pick your own plug-in name, IDs and settings key from the start.
