// Sisir for Maya: registration, the Sisir menu, and the C entry point renderer translators use.

#include "DccShared.h"
#include "ExportAbcCommand.h"
#include "FileTranslator.h"
#include "HairData.h"
#include "HairDrawOverride.h"
#include "HairExportView.h"
#include "HairShape.h"
#include "ImportCommand.h"
#include "PlyUnitCommand.h"
#include "RenderingSpace.h"

#include <maya/MDrawRegistry.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>
#include <maya/MPlug.h>
#include <maya/MSelectionMask.h>

using namespace SisirMaya;

namespace {

const char* kVendor = "Sisir";
const char* kVersion = "1.3.0";

const char* kMenuMel = R"MEL(
// Maya's own file dialogs, run outside any command (see ImportCommand.cpp for why).
global proc sisirImportBrowse() {
	string $files[] = `fileDialog2 -fileMode 1 -caption "Import Sisir Scene" -fileFilter "Sisir Scenes (*.sir)"`;
	if (size($files) > 0) sisirImport -file $files[0];
}

global proc sisirLocateAndImport(string $sirFile) {
	confirmDialog -title "Sisir Import" -button "OK" -message "Sisir was not found on this computer. Show where Sisir.exe is.";
	string $exe[] = `fileDialog2 -fileMode 1 -caption "Locate Sisir.exe" -fileFilter "Sisir (Sisir.exe)"`;
	if (size($exe) > 0) sisirImport -file $sirFile -sisir $exe[0];
}

// Sisir's order: where to, then the export dialog (sisirExportAbc shows it).
global proc sisirExportAbcBrowse() {
	if (`sisirExportAbc -check` == 0) {
		confirmDialog -title "Export Hair (ABC)" -button "OK" -message "Select the Sisir hair to export, or a group that contains it.";
		return;
	}
	string $files[] = `fileDialog2 -fileMode 0 -caption "Export Hair" -fileFilter "Alembic Files (*.abc)"`;
	if (size($files) > 0) sisirExportAbc -file $files[0];
}

global proc sisirBuildMenu() {
	global string $gMainWindow;
	if (`menu -exists sisirMainMenu`) deleteUI sisirMainMenu;
	if ($gMainWindow == "") return;
	menu -parent $gMainWindow -tearOff true -label "Sisir" sisirMainMenu;
	menuItem -label "Import Sisir Scene..." -annotation "Bake and import a Sisir .sir scene" -command "sisirImportBrowse";
	menuItem -divider true;
	menuItem -label "Export Hair (ABC)..." -annotation "The selected Sisir hair (and the visible hair in selected groups) as an Alembic groom laid out like Sisir's own export" -command "sisirExportAbcBrowse";
	menuItem -label "Export Guides (PLY)..." -annotation "The selected curves (and the visible curves in selected groups) as Sisir guides, one layer per group" -command "python(\"import Maya_curves_to_PLY; Maya_curves_to_PLY.export_selected_curves_to_ply()\")";
}
)MEL";

} // namespace

// Renderer translators (SisirMtoA, ...) find this with GetProcAddress; see HairExportView.h.
extern "C" __declspec(dllexport) bool sisirGetHairExportView(const MObject* shapeNode, SisirHairExportView* out) {
	if (!shapeNode || !out || shapeNode->isNull()) return false;
	MFnDependencyNode fn(*shapeNode);
	if (fn.typeId() != HairShape::id) return false;
	auto* shape = dynamic_cast<HairShape*>(fn.userNode());
	auto b = shape ? shape->buffers() : nullptr;
	if (!b || b->strandCount() == 0) return false;
	// The view's pointers outlive this call only while the shape keeps the same buffers.
	static thread_local std::shared_ptr<const HairBuffers> keepAlive;
	keepAlive = b;
	*out = SisirHairExportView();
	out->strandCount = b->strandCount();
	out->pointCount = b->pointCount();
	out->pointCounts = b->pointCounts.data();
	out->positions = b->positions.data();
	out->widths = b->widths.data();
	out->albedo = b->albedo.data();
	out->rootUvs = b->rootUvs.data();
	out->clumpIds = b->clumpIds.data();
	out->randoms = b->randoms.data();
	MPlug(*shapeNode, HairShape::aWidthScale).getValue(out->widthScale);
	MPlug(*shapeNode, HairShape::aRenderPercent).getValue(out->renderPercent);
	renderingSpaceMatrix(out->toRenderingSpace);
	return true;
}

MStatus initializePlugin(MObject obj) {
	MFnPlugin plugin(obj, kVendor, kVersion, "Any");
	MStatus status;
	setHost(HostInfo{ "SisirForMaya", "Maya", "maya", "Project sourceimages" });

	status = plugin.registerData(HairData::typeName, HairData::id, HairData::creator, MPxData::kGeometryData);
	if (!status) { status.perror("register sisirHairData"); return status; }

	MSelectionMask::registerSelectionType(HairShape::typeName, 2);
	status = plugin.registerShape(HairShape::typeName, HairShape::id, HairShape::creator, HairShape::initialize,
	                              nullptr, &HairShape::drawDbClassification);
	if (!status) { status.perror("register sisirHairShape"); return status; }

	status = MHWRender::MDrawRegistry::registerSubSceneOverrideCreator(HairShape::drawDbClassification,
	                                                                   HairShape::drawRegistrantId, HairDrawOverride::creator);
	if (!status) { status.perror("register hair draw override"); return status; }
	// <module>/plug-ins/<version>/SisirForMaya.mll -> <module>/shaders
	HairDrawOverride::setShaderDirectory(plugin.loadPath() + "/../../shaders");

	status = plugin.registerCommand(ImportCommand::kName, ImportCommand::creator, ImportCommand::newSyntax);
	if (!status) { status.perror("register sisirImport"); return status; }
	status = plugin.registerCommand(ExportAbcCommand::kName, ExportAbcCommand::creator, ExportAbcCommand::newSyntax);
	if (!status) { status.perror("register sisirExportAbc"); return status; }
	status = plugin.registerCommand(PlyUnitCommand::kName, PlyUnitCommand::creator, PlyUnitCommand::newSyntax);
	if (!status) { status.perror("register sisirPlyUnit"); return status; }
	status = plugin.registerFileTranslator(FileTranslator::kName, "", FileTranslator::creator, "", "", false);
	if (!status) { status.perror("register Sisir file translator"); return status; }

	MGlobal::executeCommand(kMenuMel, false, false);
	if (MGlobal::mayaState() == MGlobal::kInteractive) {
		MGlobal::executeCommandOnIdle("sisirBuildMenu", false);
	}
	return MS::kSuccess;
}

MStatus uninitializePlugin(MObject obj) {
	MFnPlugin plugin(obj);
	if (MGlobal::mayaState() == MGlobal::kInteractive) {
		MGlobal::executeCommand("if (`menu -exists sisirMainMenu`) deleteUI sisirMainMenu;", false, false);
	}
	plugin.deregisterFileTranslator(FileTranslator::kName);
	plugin.deregisterCommand(PlyUnitCommand::kName);
	plugin.deregisterCommand(ExportAbcCommand::kName);
	plugin.deregisterCommand(ImportCommand::kName);
	MHWRender::MDrawRegistry::deregisterSubSceneOverrideCreator(HairShape::drawDbClassification, HairShape::drawRegistrantId);
	plugin.deregisterNode(HairShape::id);
	MSelectionMask::deregisterSelectionType(HairShape::typeName);
	plugin.deregisterData(HairData::id);
	return MS::kSuccess;
}
