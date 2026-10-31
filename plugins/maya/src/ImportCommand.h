#pragma once

// sisirImport: bake a .sir with Sisir, let the user pick what to bring in, build it.
//
//   sisirImport -file "C:/heads/anna.sir";                       // dialog, as File > Import does
//   sisirImport -file "anna.sir" -noDialog -options "cards=0;guideDegree=3";
//   sisirImport -file "anna.sir" -noDialog -sisir "D:/Sisir/Sisir.exe";
//
// Returns the root group. Undo removes every node the import created, as one step.

#include "DccShared.h"

#include <maya/MObjectHandle.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

#include <QString>

#include <vector>

namespace SisirMaya {

struct ImportRequest {
	QString file;
	ImportOptions options;
	bool interactive = true; // progress UI and error dialogs
	bool showDialog = true;  // the options dialog
};

struct ImportOutcome {
	bool ok = false;
	bool cancelled = false;
	bool dialogShown = false; // interactive: the options dialog is up; its Import runs the import
	QString error;
	QString rootName;
	ImportOptions options; // what was actually imported (the dialog's choices)
	std::vector<MObjectHandle> created;
};

// The whole import; the command and the file translator both call it.
ImportOutcome importSirFile(const ImportRequest& request);

class ImportCommand : public MPxCommand {
public:
	static const char* kName;
	static void* creator() { return new ImportCommand(); }
	static MSyntax newSyntax();

	MStatus doIt(const MArgList& args) override;
	MStatus redoIt() override;
	MStatus undoIt() override;
	bool isUndoable() const override { return m_undoable; }
	bool hasSyntax() const override { return true; }

private:
	ImportRequest m_request;
	std::vector<MObjectHandle> m_created;
	bool m_undoable = false;
};

// Deletes what an import created (skipping anything already gone).
void deleteCreated(const std::vector<MObjectHandle>& created);

} // namespace SisirMaya
