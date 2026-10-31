# Sisir for Maya: SisirForMaya.mll and, when MtoA's SDK is present, the SisirMtoA extension.
#
# Separate from the Sisir app build on purpose: it compiles against one Maya's devkit and that
# Maya's own Qt, never vcpkg's. Configure once per Maya version:
#
#   cmake -S plugins/maya -B build_maya2025 -DMAYA_VERSION=2025
#   cmake --build build_maya2025 --config Release
#
# MAYA_LOCATION defaults to C:/Program Files/Autodesk/Maya<version>; MTOA_LOCATION to
# C:/Program Files/Autodesk/Arnold/Maya<version>. Output lands in
# <build>/module/SisirForMaya/{plug-ins,extensions,shaders,scripts}/<version>, ready to package.
cmake_minimum_required(VERSION 3.21)
project(SisirForMaya LANGUAGES CXX)

set(MAYA_VERSION "2025" CACHE STRING "Maya version to build against")
set(MAYA_LOCATION "C:/Program Files/Autodesk/Maya${MAYA_VERSION}" CACHE PATH "Maya install with devkit headers")
set(MTOA_LOCATION "C:/Program Files/Autodesk/Arnold/Maya${MAYA_VERSION}" CACHE PATH "MtoA install (optional)")

if(NOT EXISTS "${MAYA_LOCATION}/include/maya/MFnPlugin.h")
  message(FATAL_ERROR "No Maya devkit headers in ${MAYA_LOCATION}/include")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SISIR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(MODULE_DIR "${CMAKE_BINARY_DIR}/module/SisirForMaya")

# --- Maya's Qt: headers ship zipped; moc ships in bin ------------------------------------------
file(GLOB _qt_zip "${MAYA_LOCATION}/include/qt_*-include.zip")
if(NOT _qt_zip)
  message(FATAL_ERROR "Maya's Qt headers (qt_*-include.zip) not found in ${MAYA_LOCATION}/include")
endif()
list(GET _qt_zip 0 _qt_zip)
string(REGEX MATCH "qt_([0-9]+)\\.([0-9]+)\\.([0-9]+)" _qt_ver "${_qt_zip}")
set(QT_MAJOR ${CMAKE_MATCH_1})
# Only the three modules the plug-in uses (about 1,800 of the zip's 6,000 headers), unpacked once
# per Maya version outside the repository: %LOCALAPPDATA%/Sisir/maya_devkit/<version>/qt_include.
if(DEFINED ENV{LOCALAPPDATA})
  file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}" _local_app_data)
  set(_devkit_cache "${_local_app_data}/Sisir/maya_devkit")
else()
  set(_devkit_cache "${CMAKE_BINARY_DIR}/maya_devkit")
endif()
set(QT_INCLUDE "${_devkit_cache}/${MAYA_VERSION}/qt_include" CACHE PATH "Maya's Qt headers, unpacked")
if(NOT EXISTS "${QT_INCLUDE}/QtWidgets/QWidget")
  message(STATUS "Unpacking QtCore, QtGui and QtWidgets headers from ${_qt_zip} to ${QT_INCLUDE}")
  file(ARCHIVE_EXTRACT INPUT "${_qt_zip}" DESTINATION "${QT_INCLUDE}"
       PATTERNS "QtCore/*" "QtGui/*" "QtWidgets/*")
endif()
set(QT_MOC "${MAYA_LOCATION}/bin/moc.exe")
set(QT_LIBS
  "${MAYA_LOCATION}/lib/Qt${QT_MAJOR}Core.lib"
  "${MAYA_LOCATION}/lib/Qt${QT_MAJOR}Gui.lib"
  "${MAYA_LOCATION}/lib/Qt${QT_MAJOR}Widgets.lib")

# --- Alembic, linked statically -----------------------------------------------------------------
# Sisir's own ABC writer (src/ExportAbcWriter) is compiled in, against vcpkg's static Alembic and
# Imath built for the dynamic CRT (x64-windows-static-md). Nothing is exported from the .mll and no
# Alembic or Imath DLL ships, so it cannot clash with Maya's AbcExport or another plug-in's copy.
#   vcpkg install alembic:x64-windows-static-md
set(ALEMBIC_STATIC_ROOT "$ENV{USERPROFILE}/vcpkg/installed/x64-windows-static-md" CACHE PATH
    "vcpkg's x64-windows-static-md install holding Alembic and Imath")
file(TO_CMAKE_PATH "${ALEMBIC_STATIC_ROOT}" ALEMBIC_STATIC_ROOT)
if(NOT EXISTS "${ALEMBIC_STATIC_ROOT}/include/Alembic/AbcGeom/All.h")
  message(FATAL_ERROR "No static Alembic in ${ALEMBIC_STATIC_ROOT}: vcpkg install alembic:x64-windows-static-md")
endif()
file(GLOB _imath_lib "${ALEMBIC_STATIC_ROOT}/lib/Imath-*.lib")
set(ALEMBIC_STATIC_LIBS "${ALEMBIC_STATIC_ROOT}/lib/Alembic.lib" ${_imath_lib})

# moc for the Q_OBJECT headers, with Maya's moc so the generated code matches Maya's Qt.
function(sisir_moc out_var)
  set(generated)
  foreach(header ${ARGN})
    get_filename_component(name "${header}" NAME_WE)
    set(out "${CMAKE_BINARY_DIR}/moc/moc_${name}.cpp")
    add_custom_command(OUTPUT "${out}"
      COMMAND "${QT_MOC}" "${header}" -o "${out}"
      DEPENDS "${header}" VERBATIM)
    list(APPEND generated "${out}")
  endforeach()
  set(${out_var} ${generated} PARENT_SCOPE)
endfunction()

set(MAYA_DEFINES NT_PLUGIN REQUIRE_IOSTREAM _BOOL NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
set(MAYA_LIBS
  "${MAYA_LOCATION}/lib/Foundation.lib"
  "${MAYA_LOCATION}/lib/OpenMaya.lib"
  "${MAYA_LOCATION}/lib/OpenMayaRender.lib"
  "${MAYA_LOCATION}/lib/OpenMayaUI.lib"
  "${MAYA_LOCATION}/lib/OpenMayaAnim.lib")

# --- SisirForMaya.mll ---------------------------------------------------------------------------
sisir_moc(PLUGIN_MOC
  "${SISIR_ROOT}/src/ResetToDefault.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/../common/ImportDialog.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/../common/Baker.h")

add_library(SisirForMaya SHARED
  src/PluginMain.cpp
  src/DccShared.h
  src/HairData.h
  src/HairData.cpp
  src/HairShape.h
  src/HairShape.cpp
  src/HairDrawOverride.h
  src/HairDrawOverride.cpp
  src/HairExportView.h
  src/ImportCommand.h
  src/ImportCommand.cpp
  src/ExportAbcCommand.h
  src/ExportAbcCommand.cpp
  src/PlyUnitCommand.h
  src/PlyUnitCommand.cpp
  src/FileTranslator.h
  src/FileTranslator.cpp
  src/SceneBuilder.h
  src/SceneBuilder.cpp
  src/RenderingSpace.h
  src/RenderingSpace.cpp
  ../common/Baker.h
  ../common/Baker.cpp
  ../common/HairBuffers.h
  ../common/HairBuffers.cpp
  ../common/HairExportView.h
  ../common/HostInfo.h
  ../common/HostInfo.cpp
  ../common/ImportDialog.h
  ../common/ImportDialog.cpp
  ../common/ImportOptions.h
  ../common/ImportOptions.cpp
  ../common/PlyExportDialog.h
  ../common/PlyExportDialog.cpp
  ../common/PluginTooltips.cpp
  ../common/SirBakeReader.h
  ../common/SirBakeReader.cpp
  "${SISIR_ROOT}/src/SirBakeFormat.h"
  "${SISIR_ROOT}/src/AbcExportDialog.h"
  "${SISIR_ROOT}/src/AbcExportDialog.cpp"
  "${SISIR_ROOT}/src/ExportAbcWriter.h"
  "${SISIR_ROOT}/src/ExportAbcWriter.cpp"
  "${SISIR_ROOT}/src/ResetToDefault.cpp"
  "${SISIR_ROOT}/src/ui_style/SisirStyle.cpp"
  "${SISIR_ROOT}/src/ui_style/SisirWidgets.cpp"
  ${PLUGIN_MOC})
set_target_properties(SisirForMaya PROPERTIES SUFFIX ".mll"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${MODULE_DIR}/plug-ins/${MAYA_VERSION}"
  LIBRARY_OUTPUT_DIRECTORY_RELEASE "${MODULE_DIR}/plug-ins/${MAYA_VERSION}")
target_include_directories(SisirForMaya PRIVATE
  src ../common "${SISIR_ROOT}/src" "${SISIR_ROOT}/src/ui_style"
  "${MAYA_LOCATION}/include"
  "${QT_INCLUDE}" "${QT_INCLUDE}/QtCore" "${QT_INCLUDE}/QtGui" "${QT_INCLUDE}/QtWidgets"
  "${ALEMBIC_STATIC_ROOT}/include" "${ALEMBIC_STATIC_ROOT}/include/Imath")
target_compile_definitions(SisirForMaya PRIVATE ${MAYA_DEFINES} QT_NO_KEYWORDS
  SISIR_MAYA_VERSION=${MAYA_VERSION})
target_compile_options(SisirForMaya PRIVATE /MP /utf-8 /permissive- /Zc:__cplusplus /W3 /Zi /Brepro)
target_link_libraries(SisirForMaya PRIVATE ${MAYA_LIBS} ${QT_LIBS} ${ALEMBIC_STATIC_LIBS})
# Release with a PDB: crash dumps from users' machines stay readable. Same optimisation as without.
# /Brepro: no build time stamped into the binaries, so rebuilding unchanged code gives the same
# bytes -- Sisir's Plugins menu offers an update only when the plug-in really changed.
target_link_options(SisirForMaya PRIVATE /DEBUG /OPT:REF /OPT:ICF /Brepro)
target_link_options(SisirForMaya PRIVATE "/export:initializePlugin" "/export:uninitializePlugin")

# Viewport shaders and MEL/Python glue next to the plug-in.
add_custom_command(TARGET SisirForMaya POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "${MODULE_DIR}/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/scripts" "${MODULE_DIR}/scripts"
  # Export Guides (PLY) in the Sisir menu: the standalone script Sisir ships in Scripts/Maya.
  COMMAND ${CMAKE_COMMAND} -E copy "${SISIR_ROOT}/Scripts/Maya/Maya_curves_to_PLY.py" "${MODULE_DIR}/scripts/Maya_curves_to_PLY.py"
  # ...and the PLY writer every Sisir plug-in shares.
  COMMAND ${CMAKE_COMMAND} -E copy "${SISIR_ROOT}/plugins/common/sisir_ply.py" "${MODULE_DIR}/scripts/sisir_ply.py"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/module/SisirForMaya.mod" "${CMAKE_BINARY_DIR}/module/SisirForMaya.mod"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/module/install.mel" "${CMAKE_BINARY_DIR}/module/install.mel")

# --- SisirMtoA: the Arnold translator ----------------------------------------------------------
if(EXISTS "${MTOA_LOCATION}/include/mtoa/extension/Extension.h")
  add_library(SisirMtoA SHARED mtoa/SisirMtoA.cpp src/HairExportView.h ../common/ArnoldCurves.h)
  set_target_properties(SisirMtoA PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${MODULE_DIR}/extensions/${MAYA_VERSION}")
  target_include_directories(SisirMtoA PRIVATE src
    "${MAYA_LOCATION}/include" "${MTOA_LOCATION}/include" "${MTOA_LOCATION}/include/arnold"
    "${MTOA_LOCATION}/include/mtoa")
  # MtoA's Platform.h defines WIN32_LEAN_AND_MEAN itself, unguarded.
  set(_mtoa_defines ${MAYA_DEFINES})
  list(REMOVE_ITEM _mtoa_defines WIN32_LEAN_AND_MEAN)
  target_compile_definitions(SisirMtoA PRIVATE ${_mtoa_defines} _WINDOWS)
  target_compile_options(SisirMtoA PRIVATE /utf-8 /W3 /Zi /Brepro)
  target_link_options(SisirMtoA PRIVATE /DEBUG /OPT:REF /OPT:ICF /Brepro)
  target_link_libraries(SisirMtoA PRIVATE ${MAYA_LIBS}
    "${MTOA_LOCATION}/lib/mtoa_api.lib" "${MTOA_LOCATION}/lib/ai.lib")
  add_custom_command(TARGET SisirMtoA POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/mtoa/SisirMtoA.py" "${MODULE_DIR}/extensions/${MAYA_VERSION}/SisirMtoA.py")
  message(STATUS "MtoA SDK found: building SisirMtoA")
else()
  message(STATUS "MtoA SDK not found in ${MTOA_LOCATION}: skipping SisirMtoA")
endif()
