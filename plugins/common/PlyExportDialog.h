#pragma once

// Export Guides (PLY)'s one question in every Sisir plug-in: the unit the guide file is written in.
// Dressed like Sisir's Export Hair (ABC) dialog, and remembered per plug-in -- QSettings("Sisir",
// HostInfo::settingsApplication), "plyExportUnits" -- with the host's default unit until the first
// export (centimetres in Maya and 3ds Max, metres in Blender).

#include <QString>

class QWidget;

namespace SisirDcc {

// "mm", "cm" or "m"; empty when the user cancels. Remembers the answer.
QString askPlyUnit(QWidget* parent);

// The unit asked last (the host's default before the first export), without asking.
QString rememberedPlyUnit();

} // namespace SisirDcc
