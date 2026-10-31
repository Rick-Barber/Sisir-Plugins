# Sisir Plug-ins

The open-source DCC plug-ins of [Sisir](https://github.com/Rick-Barber), the hair and hair-card
grooming tool: **Sisir for Maya**, **Sisir for 3ds Max** and **Sisir for Blender**.

They import Sisir scenes (`.sir`): the mesh, the guides, the generated hair (rendered in Arnold,
Cycles or Eevee) and the hair cards. They also export hair as Sisir's Alembic (ABC) and curves back
to Sisir as guide PLYs.

This repository contains the plug-ins only. Sisir itself is not open source; you need a copy of
Sisir to import scenes, because the plug-ins ask `Sisir.exe` to generate the hair (see
[docs/HOW_IT_WORKS.md](docs/HOW_IT_WORKS.md)).

The code is MIT-licensed. Use it to fix or extend the official plug-ins, or as a blueprint for a
program Sisir doesn't support yet (Houdini, LightWave, Cinema 4D, Unreal, ...). Start with
[docs/NEW_HOST_GUIDE.md](docs/NEW_HOST_GUIDE.md), and read [docs/FORKING.md](docs/FORKING.md)
before you ship a modified plug-in.

## What's here

| Path | What it is |
| --- | --- |
| `plugins/maya/` | Sisir for Maya: a C++ plug-in (`SisirForMaya.mll`), an optional MtoA extension for Arnold, scripts and viewport shaders. Packaged as a Maya module. |
| `plugins/max/` | Sisir for 3ds Max: a C++ plug-in (`SisirForMax.dlo`) with Arnold (MAXtoA) translation built in, menu script and shaders. Packaged as an Autodesk ApplicationPlugins bundle. |
| `plugins/blender/sisir_for_blender/` | Sisir for Blender: a Python add-on. |
| `plugins/blender/bridge/` | `SisirBridge.exe`, the add-on's Qt half (dialogs, bake, ABC writer), because Blender has no Qt. |
| `plugins/common/` | The code every host shares: running the bake, reading `.sirbake`, the import dialog and options, the PLY export dialog, tooltips, `sisir_ply.py`. |
| `src/` | The few files the plug-ins share with Sisir itself: the `.sirbake` format (`SirBakeFormat.h`), the ABC writer and its dialog, Reset to Default, the UI style. |
| `Scripts/` | The Maya and 3ds Max guide-PLY exporters the builds package. |
| `tools/` | Build scripts for every installed host version, smoke tests, and an independent Python `.sirbake` reader (`sirbake_reader.py`). |
| `docs/` | How the plug-ins talk to Sisir, how to write one for a new program, and what a fork must change. |

## Requirements

All builds are Windows x64, like Sisir.

- Visual Studio 2022 (MSVC v143) and CMake 3.24 or newer.
- [vcpkg](https://github.com/microsoft/vcpkg).
- **Maya:** Maya 2022–2026 with its devkit headers in `<Maya>/include` (the Maya devkit, unpacked
  into the install). MtoA is optional; with it, the Arnold extension is built too.
  `vcpkg install alembic:x64-windows-static-md` (Alembic is linked statically, so it never clashes
  with Maya's own).
- **3ds Max:** the 3ds Max 2022–2026 SDK, MAXtoA (for its translation headers), the Arnold 7 SDK
  headers (MtoA's copy is fine) and a Maya install of the same Qt generation (2022–2024 for Qt 5,
  2025–2026 for Qt 6), whose devkit provides the Qt headers and moc the 3ds Max SDK lacks. The same
  static Alembic as Maya.
- **Blender:** Blender 4.2 or newer. The add-on is Python and needs no compiling. Its
  `SisirBridge.exe` builds with vcpkg's `qtbase` and `alembic` (manifest mode, `vcpkg.json`).
- To test imports: Sisir installed, and a `.sir` scene.

## Building

Everything builds Release. Each host version is a separate configure, because it compiles against
that host's own SDK and Qt.

### Maya

```bat
cmake -S plugins/maya -B build_maya2025 -DMAYA_VERSION=2025
cmake --build build_maya2025 --config Release
```

The module lands in `build_maya2025/module/SisirForMaya`. `MAYA_LOCATION`, `MTOA_LOCATION` and
`ALEMBIC_STATIC_ROOT` override the default locations (see the top of
[plugins/maya/CMakeLists.txt](plugins/maya/CMakeLists.txt)).

Or build every Maya installed here: `python tools/build_maya_plugins.py` (`--test` also runs the
smoke test in each one; set `SISIR_EXE` and `SISIR_SMOKE_SCENE` first).

### 3ds Max

```bat
cmake -S plugins/max -B build_max2025 -DMAX_VERSION=2025
cmake --build build_max2025 --config Release
```

The bundle lands in `build_max2025/bundle/SisirForMax.bundle`. `MAX_SDK`, `MAX_LOCATION`,
`ARNOLD_INCLUDE`, `MAXTOA_SDK_INCLUDE` and `QT_DEVKIT_MAYA` override the defaults (see
[plugins/max/CMakeLists.txt](plugins/max/CMakeLists.txt)).

Or build every SDK installed here: `python tools/build_max_plugins.py` (`--test` runs the smoke test
in a real 3ds Max window: close 3ds Max first and don't click in it).

### Blender (SisirBridge.exe)

```bat
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release --target SisirBridge
```

`SisirBridge.exe` must sit **next to `Sisir.exe`** (the add-on looks for it there), and it runs on
Sisir's own Qt and Alembic DLLs. So build it against the **same or an older Qt 6** and the **same
Alembic** as your Sisir. Sisir 1.3 ships Qt 6.10.0 and Alembic 1.8.10. Keep a copy of the original
`SisirBridge.exe` before you replace it.

`python tools/build_blender_addon.py` does the same build. `--test` runs the add-on headless in
every installed Blender, against the `SisirBridge.exe` next to `SISIR_EXE`.

## Installing

The easy way: Sisir's **Plugins** menu lists the Maya, 3ds Max and Blender versions on your
computer. Click one to install the plug-in that ships with Sisir. That installs the official
plug-in, not your build.

To install your own build by hand (close the host first):

- **Maya:** copy `build_maya<version>/module/SisirForMaya` to `Documents/maya/modules/` (or
  `%MAYA_APP_DIR%/modules`) and next to it write `SisirForMaya.mod`. Use
  [plugins/maya/module/SisirForMaya.mod](plugins/maya/module/SisirForMaya.mod) as the template:
  one section per Maya version. Alternatively, drag
  [plugins/maya/module/install.mel](plugins/maya/module/install.mel), placed next to the
  `SisirForMaya` folder, into a Maya viewport. The module's `userSetup.py` loads the plug-in at
  every start.
- **3ds Max:** copy `build_max<version>/bundle/SisirForMax.bundle` to
  `%APPDATA%/Autodesk/ApplicationPlugins/`. 3ds Max loads it at startup.
- **Blender:** copy `plugins/blender/sisir_for_blender` plus `plugins/common/sisir_ply.py` (into
  that folder) to `%APPDATA%/Blender Foundation/Blender/<version>/scripts/addons/`, then turn on
  *Sisir for Blender* in Edit > Preferences > Add-ons. Put your `SisirBridge.exe` next to
  `Sisir.exe`.

## Uninstalling

From Sisir: in the **Plugins** menu, a version with the plug-in installed is ticked. Click it and
confirm to remove the plug-in from that version.

By hand (close the host first):

- **Maya:** delete `Documents/maya/modules/SisirForMaya` and `Documents/maya/modules/SisirForMaya.mod`.
  To remove it from one Maya version only, delete that version's section from the `.mod` file.
- **3ds Max:** delete `%APPDATA%/Autodesk/ApplicationPlugins/SisirForMax.bundle`. To remove it from
  one version only, delete that version's two `<Components>` blocks from its `PackageContents.xml`.
- **Blender:** Edit > Preferences > Add-ons > *Sisir for Blender* > Remove. Or turn it off and
  delete `%APPDATA%/Blender Foundation/Blender/<version>/scripts/addons/sisir_for_blender`.

The plug-ins also remember which `Sisir.exe` to use in `HKEY_CURRENT_USER\Software\Sisir\SisirForMaya`
(`SisirForMax`, `SisirForBlender`), and keep the last 12 bakes in `%LOCALAPPDATA%\cache\Sisir\BakeCache`.
Both are safe to delete.

## Contributing

Bug reports and pull requests are welcome. This repository is a snapshot of the plug-ins as they
ship with Sisir, so accepted changes are merged upstream and come back here with the next Sisir
release.

## License

MIT, see [LICENSE](LICENSE). The license covers the code, not the Sisir name or logo: see
[docs/FORKING.md](docs/FORKING.md).
