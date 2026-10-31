#pragma once

// The whole import of a .sir, shared by File > Import (SirImporter.cpp) and MAXScript's
// sisir.importFile (SisirInterface.cpp) -- the counterpart of Sisir for Maya's importSirFile:
//
//   1. find Sisir.exe (Baker::findSisir), or ask where it is;
//   2. interactive: the import dialog opens at once while Sisir bakes behind it (BakeJob); its
//      Import button builds the scene with the dialog as the progress bar;
//      otherwise: bake, then build, with Max's progress bar (or the listener in batch);
//   3. the build is one undo step, "Import Sisir Scene".

#include "ImportOptions.h"

#include <QString>

namespace SisirMax {

struct ImportRequest {
	QString file;
	SisirDcc::ImportOptions options;
	bool interactive = true; // progress UI and error messages
	bool showDialog = true;  // the options dialog
};

struct ImportOutcome {
	bool ok = false;
	bool cancelled = false;
	QString error;
	QString rootName;
};

ImportOutcome importSirFile(const ImportRequest& request);

} // namespace SisirMax
