# Sisir plug-ins: the standalone build of SisirBridge.exe, the Qt half of Sisir for Blender.
#
# Blender has no Qt, so the import dialog, the bake and the Export Hair (ABC) dialog and writer run
# in this small helper, built from the same sources Sisir for Maya and 3ds Max compile in. It goes
# next to Sisir.exe (the add-on looks for it there) and uses Sisir's own Qt and Alembic DLLs, so
# build it against the same Qt 6 minor or an older one, and the same Alembic, as the Sisir you run
# it with (see README.md).
#
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
#   cmake --build build --config Release --target SisirBridge
#
# Sisir for Maya and Sisir for 3ds Max are not built from here: each compiles against one host's
# SDK and that host's own Qt, configured once per version (plugins/maya/CMakeLists.txt,
# plugins/max/CMakeLists.txt, or tools/build_maya_plugins.py and tools/build_max_plugins.py).
cmake_minimum_required(VERSION 3.24)
project(SisirPlugins LANGUAGES CXX)

if (NOT WIN32)
  message(FATAL_ERROR "SisirBridge is Windows-only, like Sisir")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets)
find_package(Alembic CONFIG REQUIRED)

add_executable(SisirBridge WIN32
  plugins/blender/bridge/SisirBridge.cpp
  plugins/blender/bridge/SisirBridge.manifest
  plugins/common/Baker.cpp
  plugins/common/Baker.h
  plugins/common/HostInfo.cpp
  plugins/common/ImportDialog.cpp
  plugins/common/ImportDialog.h
  plugins/common/ImportOptions.cpp
  plugins/common/PlyExportDialog.cpp
  plugins/common/PluginTooltips.cpp
  plugins/common/SirBakeReader.cpp
  src/AbcExportDialog.cpp
  src/ExportAbcWriter.cpp
  src/ResetToDefault.cpp
  src/ui_style/SisirStyle.cpp
  src/ui_style/SisirWidgets.cpp
)
target_include_directories(SisirBridge PRIVATE src plugins/common)
target_link_libraries(SisirBridge PRIVATE Qt6::Widgets Alembic::Alembic Advapi32)
if (MSVC)
  target_compile_options(SisirBridge PRIVATE /W4 /permissive- /utf-8 $<$<CONFIG:Release>:/Zi>)
  target_link_options(SisirBridge PRIVATE $<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()
