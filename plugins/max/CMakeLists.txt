# Sisir for 3ds Max: SisirForMax.dlo, with the Arnold (MAXtoA) translation built in.
#
# Separate from the Sisir app build on purpose, like Sisir for Maya: it compiles against one 3ds Max
# SDK and that Max's own Qt, never vcpkg's. Configure once per version:
#
#   cmake -S plugins/max -B build_max2022 -DMAX_VERSION=2022
#   cmake --build build_max2022 --config Release
#
# Output lands in <build>/bundle/SisirForMax.bundle (an Autodesk ApplicationPlugins bundle):
# Contents/<version>/SisirForMax.dlo, Contents/shaders, Contents/scripts, PackageContents.xml.
#
# The 3ds Max SDK ships no Qt. Qt keeps binary compatibility across patch releases both ways, so
# the headers and moc come from the Maya devkit with the same Qt minor (Max 2022-2024: Qt 5.15.1,
# Maya 2022-2024: 5.15.2; Max and Maya 2025-2026: 6.5.3), and the import libraries are generated
# from this Max's own Qt DLLs -- a symbol Max's Qt lacks fails the link, never the load.
cmake_minimum_required(VERSION 3.21)
project(SisirForMax LANGUAGES CXX)

set(MAX_VERSION "2022" CACHE STRING "3ds Max version to build against")
set(MAX_SDK "C:/Program Files/Autodesk/3ds Max ${MAX_VERSION} SDK/maxsdk" CACHE PATH "3ds Max SDK (maxsdk folder)")
set(MAX_LOCATION "C:/Program Files/Autodesk/3ds Max ${MAX_VERSION}" CACHE PATH "3ds Max install (its Qt DLLs)")
set(ARNOLD_INCLUDE "C:/Program Files/Autodesk/Arnold/maya2022/include/arnold" CACHE PATH
    "Arnold 7 SDK headers (MtoA's are fine: only the C API is used)")

if(NOT EXISTS "${MAX_SDK}/include/max.h")
  message(FATAL_ERROR "No 3ds Max SDK in ${MAX_SDK}")
endif()
if(NOT EXISTS "${ARNOLD_INCLUDE}/ai.h")
  message(FATAL_ERROR "No Arnold headers in ${ARNOLD_INCLUDE}")
endif()
# MAXtoA's translation interface (two headers in MAXtoA's own install; the interface has not
# changed since MAXtoA 2.x, so any version's copy does).
file(GLOB _maxtoa_sdks "C:/ProgramData/Autodesk/ApplicationPlugins/MAXtoA_*/MAXtoA_SDK/include")
list(SORT _maxtoa_sdks)
list(GET _maxtoa_sdks 0 _maxtoa_default)
set(MAXTOA_SDK_INCLUDE "${_maxtoa_default}" CACHE PATH "MAXtoA_SDK/include (MAXtoA_TranslationInterface.h)")
if(NOT EXISTS "${MAXTOA_SDK_INCLUDE}/MAXtoA_TranslationInterface.h")
  message(FATAL_ERROR "No MAXtoA SDK headers: set MAXTOA_SDK_INCLUDE to <MAXtoA>/MAXtoA_SDK/include")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SISIR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(BUNDLE_DIR "${CMAKE_BINARY_DIR}/bundle/SisirForMax.bundle")

# --- Qt: the matching Maya devkit's headers and moc, import libs from Max's DLLs -----------------
if(MAX_VERSION GREATER_EQUAL 2025)
  set(QT_MAJOR 6)
  set(_maya_candidates 2025 2026)
else()
  set(QT_MAJOR 5)
  set(_maya_candidates 2022 2023 2024)
endif()
set(QT_DEVKIT_MAYA "" CACHE PATH "Maya install whose devkit provides the Qt headers and moc")
if(NOT QT_DEVKIT_MAYA)
  foreach(m ${_maya_candidates})
    file(GLOB _zip "C:/Program Files/Autodesk/Maya${m}/include/qt_${QT_MAJOR}.*-include.zip")
    if(_zip AND EXISTS "C:/Program Files/Autodesk/Maya${m}/bin/moc.exe")
      set(QT_DEVKIT_MAYA "C:/Program Files/Autodesk/Maya${m}")
      break()
    endif()
  endforeach()
endif()
file(GLOB _qt_zip "${QT_DEVKIT_MAYA}/include/qt_${QT_MAJOR}.*-include.zip")
if(NOT _qt_zip)
  message(FATAL_ERROR "No Qt ${QT_MAJOR} headers found: set QT_DEVKIT_MAYA to a Maya install with its devkit")
endif()
list(GET _qt_zip 0 _qt_zip)
string(REGEX MATCH "qt_([0-9]+\\.[0-9]+\\.[0-9]+)" _ "${_qt_zip}")
set(_qt_version "${CMAKE_MATCH_1}")
if(DEFINED ENV{LOCALAPPDATA})
  file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}" _local_app_data)
  set(_devkit_cache "${_local_app_data}/Sisir/max_devkit")
else()
  set(_devkit_cache "${CMAKE_BINARY_DIR}/max_devkit")
endif()
set(QT_INCLUDE "${_devkit_cache}/qt_${_qt_version}/qt_include" CACHE PATH "Qt headers, unpacked")
if(NOT EXISTS "${QT_INCLUDE}/QtWidgets/QWidget")
  message(STATUS "Unpacking QtCore, QtGui and QtWidgets headers from ${_qt_zip} to ${QT_INCLUDE}")
  file(ARCHIVE_EXTRACT INPUT "${_qt_zip}" DESTINATION "${QT_INCLUDE}" PATTERNS "QtCore/*" "QtGui/*" "QtWidgets/*")
endif()
set(QT_MOC "${QT_DEVKIT_MAYA}/bin/moc.exe")
message(STATUS "Qt ${_qt_version} headers and moc from ${QT_DEVKIT_MAYA}")

# Import libraries from DLLs: dumpbin /exports -> .def -> lib /def. Done at configure time.
get_filename_component(_msvc_bin "${CMAKE_LINKER}" DIRECTORY)
set(_dumpbin "${_msvc_bin}/dumpbin.exe")
set(_lib "${_msvc_bin}/lib.exe")
set(IMPLIB_DIR "${CMAKE_BINARY_DIR}/implib")
file(MAKE_DIRECTORY "${IMPLIB_DIR}")
function(sisir_import_lib out_var dll)
  get_filename_component(name "${dll}" NAME_WE)
  set(def "${IMPLIB_DIR}/${name}.def")
  set(implib "${IMPLIB_DIR}/${name}.lib")
  if(NOT EXISTS "${implib}" OR "${dll}" IS_NEWER_THAN "${implib}")
    if(NOT EXISTS "${dll}")
      message(FATAL_ERROR "${dll} not found")
    endif()
    execute_process(COMMAND "${_dumpbin}" /nologo /exports "${dll}" OUTPUT_VARIABLE dump RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "dumpbin failed on ${dll}")
    endif()
    string(REPLACE "\n" ";" lines "${dump}")
    set(exports "")
    foreach(line IN LISTS lines)
      # "    ordinal hint RVA      name" -- data exports included; forwarders skipped.
      if(line MATCHES "^ +[0-9]+ +[0-9A-F]+ +[0-9A-F]+ +([^ ]+)$")
        string(APPEND exports "    ${CMAKE_MATCH_1}\n")
      endif()
    endforeach()
    file(WRITE "${def}" "LIBRARY ${name}.dll\nEXPORTS\n${exports}")
    execute_process(COMMAND "${_lib}" /nologo /machine:x64 "/def:${def}" "/out:${implib}" RESULT_VARIABLE rc OUTPUT_QUIET)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "lib /def failed for ${dll}")
    endif()
  endif()
  set(${out_var} "${implib}" PARENT_SCOPE)
endfunction()

# This Max's Qt DLLs when it is installed. Building for a Max that is not installed here: another
# installed Max with the same Qt (2022-2024 all ship Autodesk's Qt 5.15.1), else, for Qt 6, the Maya
# devkit's own import libraries (Maya and Max 2025-2026 both ship Qt 6.5.3). Such a build is
# compile-checked only until someone loads it in that Max.
set(QT_DLL_DIR "")
if(EXISTS "${MAX_LOCATION}/Qt${QT_MAJOR}Core.dll")
  set(QT_DLL_DIR "${MAX_LOCATION}")
else()
  file(GLOB _other_maxes "C:/Program Files/Autodesk/3ds Max 20[0-9][0-9]")
  foreach(m ${_other_maxes})
    string(REGEX MATCH "20[0-9][0-9]" _mv "${m}")
    if(EXISTS "${m}/Qt${QT_MAJOR}Core.dll" AND ((QT_MAJOR EQUAL 5 AND _mv LESS 2025) OR (QT_MAJOR EQUAL 6 AND _mv GREATER_EQUAL 2025)))
      set(QT_DLL_DIR "${m}")
      break()
    endif()
  endforeach()
  message(WARNING "3ds Max ${MAX_VERSION} is not installed here: this build is compile-checked only")
endif()
set(QT_LIBS "")
foreach(module Core Gui Widgets)
  if(QT_DLL_DIR)
    sisir_import_lib(_implib "${QT_DLL_DIR}/Qt${QT_MAJOR}${module}.dll")
  elseif(QT_MAJOR EQUAL 6 AND EXISTS "${QT_DEVKIT_MAYA}/lib/Qt6${module}.lib")
    set(_implib "${QT_DEVKIT_MAYA}/lib/Qt6${module}.lib")
  else()
    message(FATAL_ERROR "No Qt ${QT_MAJOR} libraries for 3ds Max ${MAX_VERSION}: install it, or a Max with the same Qt")
  endif()
  list(APPEND QT_LIBS "${_implib}")
endforeach()

# Arnold: only the C functions the translator calls, from a hand-written .def -- no Arnold library
# is needed to build. ai.dll is delay-loaded, so the plug-in loads where Arnold is not installed;
# its functions run only inside MAXtoA's translation, when MAXtoA has ai.dll loaded.
set(AI_IMPLIB "${IMPLIB_DIR}/ai.lib")
add_custom_command(OUTPUT "${AI_IMPLIB}"
  COMMAND "${_lib}" /nologo /machine:x64 "/def:${CMAKE_CURRENT_SOURCE_DIR}/arnold/ai.def" "/out:${AI_IMPLIB}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/arnold/ai.def" VERBATIM)
add_custom_target(SisirAiImportLib DEPENDS "${AI_IMPLIB}")

# --- Alembic, linked statically (as Sisir for Maya: nothing can clash with Max's own Alembic) ----
set(ALEMBIC_STATIC_ROOT "$ENV{USERPROFILE}/vcpkg/installed/x64-windows-static-md" CACHE PATH
    "vcpkg's x64-windows-static-md install holding Alembic and Imath")
file(TO_CMAKE_PATH "${ALEMBIC_STATIC_ROOT}" ALEMBIC_STATIC_ROOT)
if(NOT EXISTS "${ALEMBIC_STATIC_ROOT}/include/Alembic/AbcGeom/All.h")
  message(FATAL_ERROR "No static Alembic in ${ALEMBIC_STATIC_ROOT}: vcpkg install alembic:x64-windows-static-md")
endif()
file(GLOB _imath_lib "${ALEMBIC_STATIC_ROOT}/lib/Imath-*.lib")
set(ALEMBIC_STATIC_LIBS "${ALEMBIC_STATIC_ROOT}/lib/Alembic.lib" ${_imath_lib})

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

# --- SisirForMax.dlo ----------------------------------------------------------------------------
sisir_moc(PLUGIN_MOC
  "${SISIR_ROOT}/src/ResetToDefault.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/../common/ImportDialog.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/../common/Baker.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/HairRollout.h")

add_library(SisirForMax SHARED
  src/PluginMain.cpp
  src/ClassIds.h
  src/MaxUtil.h
  src/MaxUtil.cpp
  src/HairObject.h
  src/HairObject.cpp
  src/HairRenderItem.h
  src/HairRenderItem.cpp
  src/HairRollout.h
  src/HairRollout.cpp
  src/HairTranslator.h
  src/HairTranslator.cpp
  src/RenderingSpace.h
  src/RenderingSpace.cpp
  src/ObjReader.h
  src/ObjReader.cpp
  src/SceneBuilder.h
  src/SceneBuilder.cpp
  src/ImportFlow.h
  src/ImportFlow.cpp
  src/SirImporter.cpp
  src/ExportAbc.h
  src/ExportAbc.cpp
  src/SisirInterface.cpp
  ../common/ArnoldCurves.h
  ../common/Baker.h
  ../common/Baker.cpp
  ../common/HairBuffers.h
  ../common/HairBuffers.cpp
  ../common/HairExportView.h
  ../common/HairViewColor.h
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
  src/SisirForMax.def
  ${PLUGIN_MOC})
add_dependencies(SisirForMax SisirAiImportLib)
set_target_properties(SisirForMax PROPERTIES SUFFIX ".dlo"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${BUNDLE_DIR}/Contents/${MAX_VERSION}"
  LIBRARY_OUTPUT_DIRECTORY_RELEASE "${BUNDLE_DIR}/Contents/${MAX_VERSION}")
target_include_directories(SisirForMax PRIVATE
  # The SDK before Sisir's src: both have a mesh.h and a log.h, and the SDK's own headers include theirs.
  src ../common "${MAX_SDK}/include" "${MAX_SDK}/include/Qt" "${SISIR_ROOT}/src" "${SISIR_ROOT}/src/ui_style"
  "${MAXTOA_SDK_INCLUDE}"
  "${ARNOLD_INCLUDE}"
  "${QT_INCLUDE}" "${QT_INCLUDE}/QtCore" "${QT_INCLUDE}/QtGui" "${QT_INCLUDE}/QtWidgets"
  "${ALEMBIC_STATIC_ROOT}/include" "${ALEMBIC_STATIC_ROOT}/include/Imath")
# The SDK's own project settings (ProjectSettings/3dsmax.common.tools.settings.props).
target_compile_definitions(SisirForMax PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX _ADESK_3DSMAX_WINDOWS_ WINVER=0x0601 _WIN32_WINNT=0x0601 _WIN32_WINDOWS=0x0601
  _WIN32_IE=0x0800 _WINDOWS _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE _SCL_SECURE_NO_DEPRECATE
  ISOLATION_AWARE_ENABLED=1 _UNICODE UNICODE QT_NO_VERSION_TAGGING QT_NO_KEYWORDS
  _SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS _USE_MATH_DEFINES
  SISIR_MAX_VERSION=${MAX_VERSION})
target_compile_options(SisirForMax PRIVATE /MP /utf-8 /permissive- /Zc:__cplusplus /Zc:wchar_t /W3 /Zi /Brepro /GR /EHa
  /wd4828) # the SDK has a few headers in a legacy code page
target_link_directories(SisirForMax PRIVATE "${MAX_SDK}/lib/x64/Release")
target_link_libraries(SisirForMax PRIVATE
  core geom mesh maxutil paramblk2 maxscrpt gfx bmm poly mnmath gup
  GraphicsDriver DataBridge DefaultRenderItems MaxGraphicsObjects GraphicsUtility assetmanagement MaxQtBridge
  comctl32 ${QT_LIBS} "${AI_IMPLIB}" delayimp ${ALEMBIC_STATIC_LIBS})
# Release with a PDB: crash dumps from users' machines stay readable. /Brepro: rebuilding unchanged
# code gives the same bytes, so Sisir's Plugins menu offers an update only on a real change.
# /PDBALTPATH: only the PDB's name goes into the DLL, so a running Max (which opens the PDB the
# DLL names) never holds the build folder's copy and blocks the next build.
target_link_options(SisirForMax PRIVATE /DEBUG /OPT:REF /OPT:ICF /Brepro /DELAYLOAD:ai.dll "/PDBALTPATH:%_PDB%")

# Viewport shader, MAXScript glue and the guide exporter next to the plug-in; the package file.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/bundle/PackageContents.xml.in" "${BUNDLE_DIR}/PackageContents.xml" @ONLY)
add_custom_command(TARGET SisirForMax POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "${BUNDLE_DIR}/Contents/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/scripts" "${BUNDLE_DIR}/Contents/scripts"
  # Export Guides (PLY) in the Sisir menu: the standalone script Sisir ships in Scripts/3DSMax.
  COMMAND ${CMAKE_COMMAND} -E copy "${SISIR_ROOT}/Scripts/3DSMax/3DSMax_to_PLY.py" "${BUNDLE_DIR}/Contents/scripts/3DSMax_to_PLY.py"
  # ...and the PLY writer every Sisir plug-in shares.
  COMMAND ${CMAKE_COMMAND} -E copy "${SISIR_ROOT}/plugins/common/sisir_ply.py" "${BUNDLE_DIR}/Contents/scripts/sisir_ply.py")
