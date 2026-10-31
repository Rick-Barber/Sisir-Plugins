#include "ImportOptions.h"

#include "HostInfo.h"

#include <QSettings>

namespace SisirDcc {

namespace {
QString flag(bool b) { return b ? QStringLiteral("1") : QStringLiteral("0"); }
bool asBool(const QString& v, bool fallback) {
	if (v == "1" || v.compare("true", Qt::CaseInsensitive) == 0 || v.compare("on", Qt::CaseInsensitive) == 0) return true;
	if (v == "0" || v.compare("false", Qt::CaseInsensitive) == 0 || v.compare("off", Qt::CaseInsensitive) == 0) return false;
	return fallback;
}
// Names may hold anything but the separators; '|' separates list entries.
QString joinList(const QStringList& l) {
	QStringList escaped;
	for (QString s : l) escaped << s.replace('%', "%25").replace('|', "%7C").replace(';', "%3B").replace('=', "%3D");
	return escaped.join('|');
}
QStringList splitList(const QString& v) {
	QStringList out;
	if (v.isEmpty()) return out;
	for (QString s : v.split('|')) out << s.replace("%3D", "=").replace("%3B", ";").replace("%7C", "|").replace("%25", "%");
	return out;
}
} // namespace

QString ImportOptions::toString() const {
	QStringList kv;
	kv << "mesh=" + flag(mesh) << "refMesh=" + flag(refMesh) << "materials=" + flag(materials)
	   << "guides=" + flag(guides) << "hair=" + flag(hair) << "cards=" + flag(cards)
	   << "hairSets=" + joinList(hairSets) << "guideLayers=" + joinList(guideLayers)
	   << "cardLayers=" + joinList(cardLayers) << "includeHidden=" + flag(includeHidden)
	   << "unit=" + unit
	   << "guideDegree=" + QString::number(guideDegree)
	   << "displayPercent=" + QString::number(displayPercent) << "smoothCards=" + flag(smoothCards)
	   << "textureFolder=" + joinList({ textureFolder }) << "sisir=" + joinList({ sisirExecutable });
	return kv.join(';');
}

ImportOptions ImportOptions::fromString(const QString& s, const ImportOptions& base) {
	ImportOptions o = base;
	for (const QString& part : s.split(';', Qt::SkipEmptyParts)) {
		const int eq = part.indexOf('=');
		if (eq <= 0) continue;
		const QString key = part.left(eq).trimmed();
		const QString v = part.mid(eq + 1).trimmed();
		if (key == "mesh") o.mesh = asBool(v, o.mesh);
		else if (key == "refMesh") o.refMesh = asBool(v, o.refMesh);
		else if (key == "materials") o.materials = asBool(v, o.materials);
		else if (key == "guides") o.guides = asBool(v, o.guides);
		else if (key == "hair") o.hair = asBool(v, o.hair);
		else if (key == "cards") o.cards = asBool(v, o.cards);
		else if (key == "hairSets") o.hairSets = splitList(v);
		else if (key == "guideLayers") o.guideLayers = splitList(v);
		else if (key == "cardLayers") o.cardLayers = splitList(v);
		else if (key == "includeHidden") o.includeHidden = asBool(v, o.includeHidden);
		else if (key == "unit") {
			const QString u = v.toLower();
			if (u.isEmpty() || metresPerUnit(u) > 0.0 || u == "scene" || u == "mesh") o.unit = u;
		}
		// Before Units: Real World is the scene's own unit, Match Source Mesh the mesh file's.
		else if (key == "scale") o.unit = v.compare("realWorld", Qt::CaseInsensitive) == 0 ? "scene" : "mesh";
		else if (key == "guideDegree") o.guideDegree = v.toInt() == 3 ? 3 : 1;
		else if (key == "displayPercent") o.displayPercent = qBound(0.0, v.toDouble(), 100.0);
		else if (key == "smoothCards") o.smoothCards = asBool(v, o.smoothCards);
		else if (key == "textureFolder") o.textureFolder = splitList(v).value(0);
		else if (key == "sisir") o.sisirExecutable = splitList(v).value(0);
	}
	return o;
}

QString ImportOptions::resolvedUnit() const { return unit.isEmpty() ? host().defaultUnit : unit; }

double ImportOptions::metresPerUnit(const QString& unit) {
	if (unit == "mm") return 0.001;
	if (unit == "cm") return 0.01;
	if (unit == "m") return 1.0;
	return 0.0;
}

ImportOptions ImportOptions::loadRemembered() {
	QSettings settings("Sisir", host().settingsApplication);
	ImportOptions o = fromString(settings.value("importOptions").toString());
	// Remembered before Units existed, Scale was always saved (and was Match Source Mesh unless
	// changed): such a choice gives way to the host's default unit rather than pin the old mode.
	if (o.unit == "scene" || o.unit == "mesh") o.unit.clear();
	// Selections are per scene and the texture folder per project: never remembered.
	o.hairSets.clear();
	o.guideLayers.clear();
	o.cardLayers.clear();
	o.textureFolder.clear();
	return o;
}

void ImportOptions::remember() const {
	ImportOptions o = *this;
	o.hairSets.clear();
	o.guideLayers.clear();
	o.cardLayers.clear();
	o.textureFolder.clear();
	QSettings settings("Sisir", host().settingsApplication);
	settings.setValue("importOptions", o.toString());
}

} // namespace SisirDcc
