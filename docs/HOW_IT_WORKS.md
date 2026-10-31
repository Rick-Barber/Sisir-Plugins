# How the plug-ins talk to Sisir

A `.sir` scene holds guides, card spines and parameters, not the hair itself. The hair and the
card meshes are generated from them by Sisir's own generators, and no outside reader can reproduce
those exactly. So every plug-in works the same way:

```
 .sir ──► Sisir.exe --bake ──► .sirbake + assets folder ──► the plug-in builds the host's scene
                                (hair, guides, cards,         (curves, meshes, materials, groups)
                                 meshes, textures, META)
```

The plug-ins are pure readers of the `.sirbake`, and the bake is the whole contract between Sisir
and a plug-in. Going the other way, guides return to Sisir as PLY files (see the end of this page).

## 1. Finding Sisir.exe

[plugins/common/Baker.cpp](../plugins/common/Baker.cpp) (`Baker::findSisir`) and
[plugins/blender/sisir_for_blender/bridge.py](../plugins/blender/sisir_for_blender/bridge.py) look
in this order:

1. An explicit path (the import dialog's field, or a script argument).
2. The Sisir started last. Sisir rewrites the `.sir` file association on every launch:
   `HKCU\Software\Classes\Sisir.Scene\shell\open\command` = `"C:\...\Sisir.exe" "%1"`.
3. The one remembered from last time, or from installing the plug-in through Sisir's Plugins menu:
   `HKCU\Software\Sisir\<SisirForMaya|SisirForMax|SisirForBlender>\sisirExecutable`.

Before running it, `Baker::canBake` checks that the executable contains the text
`usage: Sisir --bake`. A Sisir older than 1.3 has no bake and would open its window instead.

## 2. The bake

```
Sisir.exe --bake <in.sir> <out.sirbake> [--assets <dir>] [--progress]
```

- Runs headless: no window, no GPU.
- `--assets` is where meshes and textures are copied (default: `<out>_assets` next to the output).
  Asset paths in the bake are relative to it.
- `--progress` prints `PROGRESS <fraction 0..1> <label>` lines on stdout while it works.
- Errors go to stderr as `ERROR <message>`.
- Exit codes (`SirBake::ExitCode` in [src/SirBakeFormat.h](../src/SirBakeFormat.h)):

| Code | Meaning |
| --- | --- |
| 0 | OK |
| 2 | Usage: wrong arguments |
| 3 | Not a readable `.sir` |
| 4 | The scene's mesh could not be restored |
| 5 | The output or assets folder is not writable |
| 6 | Out of memory |
| 7 | Hair generation failed |

`Baker` caches bakes per (.sir path, size, time) × (Sisir.exe path, size, time) × format version in
`%LOCALAPPDATA%\cache\Sisir\BakeCache` and keeps the last 12, so importing the same file twice runs
Sisir once. `BakeJob` is the non-blocking version, for a dialog that stays responsive.

## 3. The .sirbake file

[src/SirBakeFormat.h](../src/SirBakeFormat.h) is the specification. It has no dependencies, so
you can copy it as-is. All values are little-endian.

```
FileHeader   (32 bytes)  magic "SIRBAKE\0", formatVersion (1), chunkCount
{ ChunkHeader (24 bytes)  tag[4], index, flags, crc32 of payload, payloadSize
  payload
  zero padding to 8 bytes } × chunkCount
```

Every array inside a payload starts on an 8-byte boundary. Readers must validate sizes and CRCs
(CRC-32 as zlib computes it; `SirBake::crc32`) instead of trusting them.

| Chunk | One per | Payload |
| --- | --- | --- |
| `META` | file, always first | UTF-8 JSON, see below |
| `HAIR` | baked hair set | `HairHeader`, `u32 pointCounts[strands]`, `f32 positions[points*3]`, `f32 widths[points]`, `u8 albedo[points*4]` (sRGB, a = 255), `f32 rootUvs[strands*2]`, `i32 clumpIds[strands]`, `f32 randoms[strands]` |
| `GUID` | guide layer | `CurvesHeader`, `u32 pointCounts[curves]`, `f32 positions[points*3]` |
| `CARD` | card layer and smoothing (flags bit 0 = smooth) | `CardsHeader`, `f32 positions[v*3]`, `f32 normals[v*3]`, `f32 uvs[v*2]`, `u32 faceSizes[faces]`, `u32 corners[corners]` |

The chunk `index` is the position of its entry in the matching META list.

**Coordinates** are Sisir world space: metres, Y up, right-handed. Convert to your host's units and
up axis (Maya: centimetres Y-up; 3ds Max and Blender: Z-up). The importers in this repository show
how. **UVs** have a bottom-left origin (already flipped from Sisir's image space).

### META

```jsonc
{
  "formatVersion": 1, "sisirVersion": "1.3.0", "source": "<in.sir>", "sceneName": "<file stem>",
  "units": "m", "upAxis": "y",
  "mesh":    { "present": true, "file": "mesh/<name>.obj", "original": true, "importScale": 1.0,
               "triangles": 0, "vertices": 0, "texture": "textures/...", "mannequin": false,
               "blendshapeApplied": false },
  "refMesh": { ...same fields... },
  "meshMaterial": { "albedoColor": [r,g,b], "albedoTexture": "", "roughness": 0.5, ... },
  "guideLayers": [ { "name": "", "color": [r,g,b], "visible": true,
                     "folders": [ { "name": "", "visible": true } ], "curves": 0, "chunk": 0 } ],
  "hairSets":    [ { "name": "", "color": [r,g,b], "visible": true, "materialId": 0,
                     "strands": 0, "points": 0, "maxPointsPerStrand": 0, "chunk": 0 } ],
  "hairMaterials": [ { "melanin": 0.0, "roughness": 0.0, "ior": 1.55, ... } ],
  "cardLayers":  [ { "name": "", "objectName": "", "color": [r,g,b], "visible": true, "folders": [],
                     "uvId": 0, "facesRaw": 0, "facesSmooth": 0, "chunk": 0 } ],
  "cardMaterials": [ { "colorTexture": "card_textures/...", "maskTexture": "", "normalTexture": "",
                       "roughness": 0.5, "alphaCutoff": 0.5, ... } ]
}
```

- Paths (`file`, `*Texture`) are relative to the assets folder. An empty string means none.
- `mesh.original` is true when `file` is the user's own OBJ, untouched. When it is false, Sisir
  wrote its own geometry (for the mannequin, a non-OBJ source, or a mesh a blendshape morphed).
- `folders` is the layer's folder path in Sisir's Layers panel, outermost first. The importers
  rebuild it as nested groups.
- Hidden sets and layers are baked too, with `visible: false`. The importer decides what to do.
- A hair set's `materialId` indexes `hairMaterials`; a card layer's `uvId` indexes `cardMaterials`
  (-1 means none).
- New keys may appear in later versions. Ignore what you don't know. `formatVersion` changes only
  when the binary layout does.

The exact key list is whatever the readers here read. To see a real one:

```bat
python tools/sirbake_reader.py --bake "C:\...\Sisir.exe" scene.sir out.sirbake
python tools/sirbake_reader.py out.sirbake --json
```

### Reference readers

- C++: [plugins/common/SirBakeReader.cpp](../plugins/common/SirBakeReader.cpp) (Maya, 3ds Max, SisirBridge).
- Python, inside a host: [plugins/blender/sisir_for_blender/sirbake.py](../plugins/blender/sisir_for_blender/sirbake.py).
- Python, standalone, no dependencies, validates everything:
  [tools/sirbake_reader.py](../tools/sirbake_reader.py). Use it to check that your own reader
  agrees.

## 4. What plugins/common gives a C++ plug-in

| File | Purpose |
| --- | --- |
| `Baker.*` | Find Sisir, run the bake with progress and cancel, the bake cache, `BakeJob` |
| `SirBakeReader.*` | Read and validate a `.sirbake` |
| `ImportDialog.*`, `ImportOptions.*` | Sisir's import dialog (which sets and layers, units, hair density, materials) and its option string |
| `HostInfo.*` | Per-host identity: settings name, display name, texture folder |
| `HairBuffers.*` | One hair set's strands as the host's hair object holds them (shared, immutable buffers) |
| `HairViewColor.h` | Viewport colours, so a material looks the same in every host |
| `HairExportView.h` | The C interface a renderer translator reads hair through, so a V-Ray or Redshift translator can be a separate DLL built against that renderer's SDK |
| `ArnoldCurves.h` | Hair as an Arnold `curves` node (SisirMtoA and the 3ds Max MAXtoA translation both use it) |
| `PlyExportDialog.*`, `sisir_ply.py` | Export Guides (PLY): the unit popup and the shared writer |
| `PluginTooltips.cpp` | Tooltip texts for the shared dialogs (the `UiTooltips` service in `src/`) |
| `MetaFolders.h` | META folder paths as host groups |

The dialogs are Qt widgets. They compile against the host's own Qt (Maya and 3ds Max ship one),
never a separate copy.

## 5. Hosts without Qt: SisirBridge

Blender has no Qt, so its add-on is plain Python. Everything Qt-based runs in a separate helper,
[plugins/blender/bridge/SisirBridge.cpp](../plugins/blender/bridge/SisirBridge.cpp), which sits
next to `Sisir.exe`. The add-on starts it and talks over stdin/stdout in UTF-8 lines:

```
SisirBridge import <in.sir> [--owner <hwnd>] [--options <s>] [--sisir <exe>] [--assets <dir>]
SisirBridge bake <in.sir> [--assets <dir>] [--sisir <exe>]
SisirBridge export-abc <in.bin> <out.abc> [--options <s>] [--no-dialog] [--owner <hwnd>]
SisirBridge ply-units [--owner <hwnd>]
```

Every run first prints `SISIR_BRIDGE <protocol>` (currently 1). The full protocol (PROGRESS,
IMPORT, RESULT, CANCEL and ERROR lines) is in the comment at the top of `SisirBridge.cpp`.
`SisirBridge` is not specific to Blender: any host that can start a process and read its output
can use it.

## 6. Back to Sisir: guide PLYs

Each plug-in's **Export Guides (PLY)** writes the selected curves in the format Sisir's
**File > Import Curves (PLY)** reads. All three plug-ins write it with one writer,
[plugins/common/sisir_ply.py](../plugins/common/sisir_ply.py) (Python 2.7 and 3, no dependencies).
Its docstring is the format specification: an ASCII PLY with a `sisir_unit` comment, layer comments,
and per-vertex `anchor`, `layer_id` and `curve_id`. Reuse it as-is.

## 7. Hair as Alembic

**Export Hair (ABC)** writes hair the way Sisir's own exporter does, with
[src/ExportAbcWriter.cpp](../src/ExportAbcWriter.cpp) and its dialog
[src/AbcExportDialog.cpp](../src/AbcExportDialog.cpp). A host that can already read Alembic curves
can take hair that way without a plug-in, but loses the materials, groups and cards the importers
build.
