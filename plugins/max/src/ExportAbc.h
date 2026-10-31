#pragma once

// Sisir > Export Hair (ABC): the selected Sisir hair as the Alembic groom Sisir itself writes
// (src/ExportAbcWriter, the same writer and dialog as the app and Sisir for Maya), back in Sisir's
// Y-up metres scaled to the chosen unit.
//
// What goes: the selected Sisir Hair objects (even hidden ones selected by name), and under any other
// selected node every visible Sisir Hair object at any depth -- never anything unselected.

#include <QString>

namespace SisirMax {

// How many hair objects the selection would export.
int exportAbcTargetCount();

// `options`: "units=cm;color=1;unreal=0..." over the remembered settings. With `showDialog` (and a
// user present) Sisir's export dialog asks first. Returns the written file, or empty.
QString exportAbc(const QString& file, const QString& options, bool showDialog);

// The menu's flow: check the selection, ask where to, then the export dialog.
void exportAbcInteractive();

} // namespace SisirMax
