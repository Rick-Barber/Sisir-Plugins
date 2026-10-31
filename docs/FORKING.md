# Forking a Sisir plug-in

You are free to modify these plug-ins and to ship your own versions (see [LICENSE](../LICENSE)). If
your version is only ever used on its own, change what you like. But if it may be installed
**alongside the official plug-in**, or open scenes saved with it, you must change the identifiers
below. Otherwise the two collide: scenes load the wrong type, settings get shared, or Sisir
overwrites your plug-in.

## Must change

### The plug-in and folder names

Sisir's **Plugins** menu installs and updates the official plug-ins by name, and decides whether
to update by comparing a fingerprint of the files (`sisir_plugin_fingerprint.txt` in the installed
copy). A modified plug-in installed under the official name shows as **(Update)** in that menu, and
clicking it **replaces your version with the official one**.

| Host | Official name (rename yours) |
| --- | --- |
| Maya | module folder `SisirForMaya`, `SisirForMaya.mod`, plug-in `SisirForMaya.mll` (also in `userSetup.py`, `install.mel` and `plugins/maya/CMakeLists.txt`) |
| 3ds Max | bundle `SisirForMax.bundle`, `SisirForMax.dlo`, and the `ProductCode` and `UpgradeCode` GUIDs in `plugins/max/bundle/PackageContents.xml.in` (generate new ones) |
| Blender | add-on folder `sisir_for_blender`, `bl_info["name"]` |

### Maya type IDs

Maya scenes store node and data type IDs. Sisir's block, `0x00143000`–`0x0014303F`, is registered
to Sisir with Autodesk ([plugins/maya/src/PluginIds.h](../plugins/maya/src/PluginIds.h)). A fork
that registers its own node types must request its **own ID block** from Autodesk (free, through
the Autodesk Developer Network) and use it instead. Two plug-ins registering the same ID cannot be
loaded together, and a scene saved with one opens with the wrong type in the other.

Also rename the node and data type names (`sisirHairShape` and friends) and the MEL/Python command
names (`sisirImport`, `sisirExportAbc`, `sisirPlyUnit`) and the `Sisir Scene` file translator,
which must be unique in a Maya session.

### 3ds Max class IDs

`.max` files store class IDs. Generate new ones with `gencid.exe` from the 3ds Max SDK, and replace
every ID in [plugins/max/src/ClassIds.h](../plugins/max/src/ClassIds.h): the hair object, the
importer and the function-publishing interface. Also rename the MAXScript global (`sisir`) and the
class names shown in the Create panel.

### Blender identifiers

Operator and menu `bl_idname`s (`sisir.import_scene`, `TOPBAR_MT_sisir`, `SISIR_FH_sir`, ...) must
be unique across all enabled add-ons. Give yours a different prefix.

### Settings

The plug-ins store their settings (remembered Sisir.exe, dialog choices) under
`HKEY_CURRENT_USER\Software\Sisir\<settingsApplication>`. `settingsApplication` is set by each
host's `HostInfo` (see `plugins/maya/src/PluginMain.cpp`, `plugins/max/src/PluginMain.cpp`, and
`SETTINGS_KEY` in `plugins/blender/sisir_for_blender/options.py`). Use your own so the two don't
overwrite each other's choices.

## Keep as they are

- **`Sisir.exe --bake` and the `.sirbake` format.** These are Sisir's interface, not the plug-in's.
  Don't change `SirBakeFormat.h` in a way that stops your reader from reading the files Sisir
  writes.
- **The guide PLY format** (`sisir_ply.py`), so Sisir can still import what your plug-in exports.
- **The SisirBridge protocol**, if you use the official `SisirBridge.exe`. If you change it, ship
  your own bridge under a different name.

## The Sisir name

MIT covers the code. It does not give you the right to use the Sisir name or logo for your
product. Name your fork something of your own, for example "Sisir importer for Houdini by
<you>", not "Sisir for Houdini". Don't present it as official. Mentioning that it works with
Sisir is fine.
