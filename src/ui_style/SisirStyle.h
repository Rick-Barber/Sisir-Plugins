#pragma once

// Sisir's look, in one place: the dark palette and the stylesheet every Sisir window wears.
//
// Plain QtWidgets, Qt 5.15 and Qt 6 alike, and nothing from the app: the DCC plugins (Maya's Qt,
// Max's Qt) compile this same file, so their dialogs cannot drift from the app.

#include <QColor>
#include <QPalette>
#include <QString>

namespace SisirStyle {

// The fixed colours.
inline const QColor kWindow{ 0x16, 0x19, 0x1D };  // window background
inline const QColor kPanel{ 0x1F, 0x23, 0x28 };   // dock titles, buttons, menus
inline const QColor kBase{ 0x10, 0x12, 0x14 };    // fields, lists
inline const QColor kBorder{ 0x2B, 0x31, 0x38 };  // borders, hover
inline const QColor kText{ 0xC9, 0xD1, 0xD9 };
inline const QColor kMuted{ 0x58, 0x60, 0x69 };   // disabled text, scrollbar hover
inline const QColor kNote{ 0x8B, 0x94, 0x9E };    // status text
// The viewport's debug light blue (kCombVectorSelectedColor, 0.45 / 0.90 / 1.00) for UI that
// points at something the same way: the Layers panel's drop target while dragging.
inline const QColor kDropHighlight{ 0x73, 0xE6, 0xFF };

// Per-module accents. The Guides module's blue is also the default.
inline constexpr const char* kAccentGuides = "#3A7BFF";
inline constexpr const char* kAccentHair = "#B33939";
inline constexpr const char* kAccentCards = "#BF7B33";
inline constexpr const char* kAccentTexture = "#34612C";
inline constexpr const char* kAccentRender = "#1D2F49";

// The palette QtApp installs application-wide. A plugin sets it on its own top-level widget.
QPalette palette();

// The main window's stylesheet for `accent` (a #RRGGBB string).
QString styleSheet(const QString& accent);

} // namespace SisirStyle
