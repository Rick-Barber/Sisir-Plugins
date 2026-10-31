#pragma once

// File > Export Hair (ABC)'s dialogs, shared with the DCC plug-ins so exporting from Maya asks the
// same questions, remembers the same answers (QSettings "Sisir"), and warns the same way.

#include "ExportAbcWriter.h"

class QWidget;

namespace AbcExportDialog {

// The units and groom attributes. Returns false when the user cancels; on accept, `outScale` is
// the m -> target-unit factor and `io` carries the chosen options in and the user's answer out.
bool show(QWidget* parent, float& outScale, ExportAbc::Options& io);

// The Unreal 255-points-per-strand warning, for a groom with `maxPoints` points on its longest
// strand. Returns whether to go ahead with the export.
bool confirmUnrealPointLimit(QWidget* parent, int maxPoints);

// The choices `show` last saved (its defaults before the first export), without asking: for
// scripted exports that skip the dialog.
void loadRemembered(float& outScale, ExportAbc::Options& io);

} // namespace AbcExportDialog
