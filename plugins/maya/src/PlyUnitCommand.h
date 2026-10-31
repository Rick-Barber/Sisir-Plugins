#pragma once

// sisirPlyUnit: the unit Export Guides (PLY) writes in (plugins/common/PlyExportDialog).
//
//   sisirPlyUnit;                          // the unit asked last ("mm", "cm" or "m"), without asking
//   sisirPlyUnit -file "C:/out/guides.ply"; // asks in Sisir's look, then exports the selection there
//
// With -file the dialog opens from Maya's event loop once the command has returned (as
// sisirExportAbc's does), then Maya_curves_to_PLY.py writes the file in the chosen unit. Changes
// nothing in the scene, so not undoable.

#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

namespace SisirMaya {

class PlyUnitCommand : public MPxCommand {
public:
	static const char* kName;
	static void* creator() { return new PlyUnitCommand(); }
	static MSyntax newSyntax();
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override { return false; }
	bool hasSyntax() const override { return true; }
};

} // namespace SisirMaya
