#pragma once

// Everything the import can be told: what to bring in and how. One struct serves the dialog,
// the sisirImport command flags, the file translator's option string and the remembered settings,
// so the four can never disagree on a name or a default.

#include <QString>
#include <QStringList>

namespace SisirDcc {

struct ImportOptions {
	// What
	bool mesh = true;
	bool refMesh = true;
	bool materials = true;   // Arnold shaders + textures
	bool guides = true;
	bool hair = true;
	bool cards = true;
	// Which (names as Sisir shows them; empty = every visible one)
	QStringList hairSets;
	QStringList guideLayers;
	QStringList cardLayers;
	bool includeHidden = false; // hidden sets/layers too, when no explicit list is given

	// How
	// What one scene unit stands for: "mm", "cm" or "m" -- the scene's numbers are Sisir's metres
	// in that unit. Empty: the host's default (HostInfo::defaultUnit). Older option strings may also
	// give "scene" (scale=realWorld: the scene's own unit) or "mesh" (scale=matchMesh: the mesh
	// file's unit).
	QString unit;
	QString resolvedUnit() const;
	// Metres per unit for "mm", "cm" and "m"; 0 for anything else ("scene", "mesh").
	static double metresPerUnit(const QString& unit);
	int guideDegree = 1;        // 1 linear (Sisir's points exactly) or 3 cubic through them
	double displayPercent = 100.0;
	bool smoothCards = true;
	QString textureFolder;      // empty: the host project's image folder, /sisir/<scene>
	QString sisirExecutable;    // empty: found automatically

	// "key=value;key=value" -- the translator option string and the -options flag.
	QString toString() const;
	static ImportOptions fromString(const QString& s, const ImportOptions& base = ImportOptions());

	// Remembered between imports (QSettings, per user).
	static ImportOptions loadRemembered();
	void remember() const;
};

inline constexpr double kDefaultDisplayPercent = 100.0;

} // namespace SisirDcc
