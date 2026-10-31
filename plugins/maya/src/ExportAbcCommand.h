#pragma once

// sisirExportAbc: Sisir hair to an Alembic groom, written by Sisir's own ABC writer
// (src/ExportAbcWriter) so the file is laid out exactly like File > Export Hair (ABC) in Sisir.
//
//   sisirExportAbc -file "C:/out/anna.abc";               // Sisir's export dialog, then export
//   sisirExportAbc -file "anna.abc" -noDialog;            // the choices the dialog last saved
//   sisirExportAbc -file "anna.abc" -noDialog -options "units=cm;color=1;unreal=1" hairSet1;
//   sisirExportAbc -check;                                // how many hair sets the selection holds
//
// Exports the given or selected sisirHairShapes, including every visible one in the hierarchies
// under selected groups; with nothing selected, nothing (an error says to select the hair). One
// curves object per shape, named after its Sisir hair set, in world space. Options: units=mm|cm|m
// and rootUv, thickness, clumpId, color, roughness, ao, unreal as 0/1. Returns the file written.
// Changes nothing in the scene, so not undoable.

#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

namespace SisirMaya {

class ExportAbcCommand : public MPxCommand {
public:
	static const char* kName;
	static void* creator() { return new ExportAbcCommand(); }
	static MSyntax newSyntax();
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override { return false; }
	bool hasSyntax() const override { return true; }
};

} // namespace SisirMaya
