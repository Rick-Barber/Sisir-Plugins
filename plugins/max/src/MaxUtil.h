#pragma once

// Small helpers every part of Sisir for 3ds Max uses: strings, MAXScript, the listener, the
// plug-in's own location, and Sisir's axes and units in Max terms.

#include <max.h>

#include <QString>

class FPValue;

namespace SisirMax {

inline MSTR toM(const QString& s) { return MSTR(reinterpret_cast<const wchar_t*>(s.utf16())); }
inline QString toQ(const MCHAR* s) { return s ? QString::fromWCharArray(s) : QString(); }
inline QString toQ(const MSTR& s) { return QString::fromWCharArray(s.data(), (int)s.Length()); }

void setHInstance(HINSTANCE instance); // once, from DllMain
HINSTANCE hInstance();
// <bundle>/Contents/<version>/SisirForMax.dlo -> <bundle>/Contents
QString contentsDir();
QString pluginPath();

// A string as a MAXScript string literal.
QString msQuote(const QString& s);
// Runs MAXScript; false (and the error in the listener) when it fails. `result` gets its value.
bool runMaxScript(const QString& script, FPValue* result = nullptr);
bool runMaxScriptBool(const QString& script);

// A 3ds Max session with a user in front of it (not 3dsmaxbatch, a render node or quiet mode).
bool interactive();

void logInfo(const QString& text);
void logWarning(const QString& text);
void logError(const QString& text);
// A message box over Max's window once the current call has returned; only when interactive.
void showErrorLater(const QString& title, const QString& text);

// Sisir is Y-up, right-handed; 3ds Max is Z-up, right-handed: (x, y, z) -> (x, -z, y), the mapping
// Scripts/3DSMax/PLY_to_3DSMax.py uses. And back: (x, y, z) -> (x, z, -y).
inline Point3 sisirToMax(float x, float y, float z, float s) { return Point3(x * s, -z * s, y * s); }
inline void maxToSisir(const Point3& p, float out[3]) {
	out[0] = p.x;
	out[1] = p.z;
	out[2] = -p.y;
}

// One metre in the scene's system units.
double systemUnitsPerMetre();

// A legal, readable node name: letters, digits and underscores (as the Maya plug-in names nodes).
QString legalName(const QString& name, const QString& fallback = "Sisir");

} // namespace SisirMax
