#pragma once

// What the code shared by Sisir for Maya and Sisir for 3ds Max (plugins/common) needs to know about
// the application it runs in. Each plug-in sets it once when it loads, before any import.

#include <QString>

namespace SisirDcc {

struct HostInfo {
	QString settingsApplication = "SisirForMaya"; // QSettings("Sisir", <this>): options, Sisir.exe
	QString displayName = "Maya";                 // "Building the <name> scene..."
	QString tooltipPrefix = "maya";               // UiTooltips ids: <prefix>.import.<control>
	QString textureFolderPlaceholder = "Project sourceimages";
	QString defaultUnit = "cm";                   // what one scene unit stands for by default: mm, cm or m
};

const HostInfo& host();
void setHost(const HostInfo& info);

// "<prefix>.import.<control>", as a stable string for UiTooltips::text.
const char* importTooltipId(const char* control);

} // namespace SisirDcc
