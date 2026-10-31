#include "ExportAbcCommand.h"

#include "AbcExportDialog.h"
#include "ExportAbcWriter.h"
#include "HairData.h"
#include "HairShape.h"
#include "ImportDialog.h"

#include <maya/MArgDatabase.h>
#include <maya/MDagPath.h>
#include <maya/MFnDagNode.h>
#include <maya/MGlobal.h>
#include <maya/MItDag.h>
#include <maya/MMatrix.h>
#include <maya/MPlug.h>
#include <maya/MPoint.h>
#include <maya/MQtUtil.h>
#include <maya/MSelectionList.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace SisirMaya {

const char* ExportAbcCommand::kName = "sisirExportAbc";

namespace {

MString toM(const QString& s) { return MString(reinterpret_cast<const wchar_t*>(s.utf16())); }
QString toQ(const MString& s) { return QString::fromUtf8(s.asUTF8()); }

QString melQuote(const QString& s) {
	QString out = s;
	out.replace("\\", "\\\\").replace("\"", "\\\"");
	return "\"" + out + "\"";
}

const QString kSelectHair = "Select the Sisir hair to export, or a group that contains it.";

bool isHairShape(const MDagPath& path) {
	return path.node().hasFn(MFn::kPluginShape) && MFnDagNode(path).typeId() == HairShape::id;
}

// The shapes to export, by full path: those in the given objects, else in the selection -- never
// anything unselected. A hair shape or its transform counts as asked for even when hidden; in the
// hierarchy under any other selected node every visible hair shape goes, at any depth (hidden ones
// stay out there, as Sisir exports only visible hair sets).
std::vector<MDagPath> resolveTargets(const MSelectionList& given) {
	MSelectionList list = given;
	if (list.length() == 0) MGlobal::getActiveSelectionList(list);
	std::vector<MDagPath> out;
	QSet<QString> seen;
	auto add = [&](const MDagPath& p) {
		const QString name = toQ(p.fullPathName());
		if (seen.contains(name)) return;
		seen.insert(name);
		out.push_back(p);
	};
	auto addUnder = [&](const MDagPath& root, bool requireVisible) {
		MItDag it(MItDag::kDepthFirst, MFn::kPluginShape);
		it.reset(root, MItDag::kDepthFirst, MFn::kPluginShape);
		for (; !it.isDone(); it.next()) {
			MDagPath p;
			if (!it.getPath(p) || !isHairShape(p)) continue;
			if (requireVisible && !p.isVisible()) continue;
			add(p);
		}
	};

	for (unsigned int i = 0; i < list.length(); i++) {
		MDagPath p;
		if (!list.getDagPath(i, p)) continue;
		if (isHairShape(p)) { add(p); continue; }
		// A transform straight above hair shapes: those shapes, visible or not.
		for (unsigned int c = 0; c < p.childCount(); c++) {
			MDagPath child = p;
			if (child.push(p.child(c)) && isHairShape(child)) add(child);
		}
		addUnder(p, true);
	}
	return out;
}

HairShape* hairShapeOf(const MDagPath& path) {
	return dynamic_cast<HairShape*>(MFnDagNode(path).userNode());
}

int maxPointsOf(const std::vector<MDagPath>& targets) {
	int maxPoints = 0;
	for (const MDagPath& p : targets) {
		HairShape* shape = hairShapeOf(p);
		auto b = shape ? shape->buffers() : nullptr;
		if (!b) continue;
		for (uint32_t c : b->pointCounts) maxPoints = std::max(maxPoints, (int)c);
	}
	return maxPoints;
}

// "units=cm;color=1;unreal=0" over `scale` and `o`. False on an unknown key or value.
bool applyOptionString(const QString& text, float& scale, ExportAbc::Options& o, QString* error) {
	for (const QString& item : text.split(';', Qt::SkipEmptyParts)) {
		const int eq = item.indexOf('=');
		const QString key = item.left(eq).trimmed();
		const QString value = eq < 0 ? QString() : item.mid(eq + 1).trimmed();
		if (key == "units") {
			if (value == "mm") scale = 1000.0f;
			else if (value == "cm") scale = 100.0f;
			else if (value == "m") scale = 1.0f;
			else { *error = "units must be mm, cm or m"; return false; }
			continue;
		}
		bool* flag = key == "rootUv" ? &o.rootUv : key == "thickness" ? &o.thickness
			: key == "clumpId" ? &o.clumpId : key == "color" ? &o.color : key == "roughness" ? &o.roughness
			: key == "ao" ? &o.ao : key == "unreal" ? &o.unrealOrientation : nullptr;
		if (!flag || (value != "0" && value != "1")) { *error = "unknown option \"" + item + "\""; return false; }
		*flag = value == "1";
	}
	return true;
}

QString optionString(float scale, const ExportAbc::Options& o) {
	const char* units = scale == 1000.0f ? "mm" : scale == 1.0f ? "m" : "cm";
	return QString("units=%1;rootUv=%2;thickness=%3;clumpId=%4;color=%5;roughness=%6;ao=%7;unreal=%8")
		.arg(units).arg(o.rootUv).arg(o.thickness).arg(o.clumpId).arg(o.color).arg(o.roughness).arg(o.ao)
		.arg(o.unrealOrientation);
}

// Metres per internal unit of `path`: what its Sisir import recorded on the root (the Units chosen
// there), else Maya's own centimetres.
double metresPerInternal(MDagPath path) {
	while (path.length() > 0) {
		MFnDagNode node(path);
		if (node.hasAttribute("sisirMetresPerInternal")) {
			const double v = node.findPlug("sisirMetresPerInternal", true).asString().asDouble();
			if (v > 0.0) return v;
		}
		path.pop();
	}
	return 0.01;
}

// One shape as the writer's CurveSet: world space, Maya's numbers to metres to the chosen unit, the
// way Sisir's exporter scales its metres.
ExportAbc::CurveSet curveSetOf(const MDagPath& path, const HairBuffers& b, float scale, const ExportAbc::Options& o) {
	ExportAbc::CurveSet set;
	set.name = MPlug(path.node(), HairShape::aHairSetName).asString().asUTF8();
	if (set.name.empty()) {
		MDagPath transform = path;
		transform.pop();
		set.name = MFnDagNode(transform).name().asUTF8();
	}
	const MMatrix world = path.inclusiveMatrix();
	const double toUnit = metresPerInternal(path) * (double)scale;
	// Strand widths follow the transform's uniform scale, as the viewport and Arnold draw them.
	const double widthScale = std::cbrt(std::fabs(world.det3x3())) *
		(double)MPlug(path.node(), HairShape::aWidthScale).asFloat() * toUnit;
	MPlug colorPlug(path.node(), HairShape::aSetColor);
	float color[3];
	for (unsigned int k = 0; k < 3; k++) color[k] = std::clamp(colorPlug.child(k).asFloat(), 0.0f, 1.0f);
	const float roughness = std::clamp(MPlug(path.node(), HairShape::aGroomRoughness).asFloat(), 0.0f, 1.0f);
	const float ao = std::clamp(MPlug(path.node(), HairShape::aGroomAo).asFloat(), 0.0f, 1.0f);

	const uint32_t strandCount = b.strandCount();
	set.positions.reserve(b.positions.size());
	set.pointCounts.reserve(strandCount);
	if (o.thickness) set.widths.reserve(b.pointCount());
	if (o.rootUv) set.rootUvs.reserve(strandCount * 2u);
	if (o.clumpId) set.clumpIds.reserve(strandCount);
	if (o.color) set.colors.reserve(b.pointCount() * 3u);
	if (o.roughness) set.roughnesses.reserve(b.pointCount());
	if (o.ao) set.aos.reserve(b.pointCount());

	std::vector<float> strand;
	for (uint32_t s = 0; s < strandCount; s++) {
		const uint32_t first = b.offsets[s], count = b.pointCounts[s];
		strand.clear();
		bool finite = count >= 2;
		for (uint32_t i = first; i < first + count && finite; i++) {
			const MPoint p = MPoint(b.positions[i * 3u], b.positions[i * 3u + 1u], b.positions[i * 3u + 2u]) * world;
			const float x = (float)(p.x * toUnit), y = (float)(p.y * toUnit), z = (float)(p.z * toUnit);
			finite = std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
			// Y-up (Maya, Sisir) to Z-up (Unreal) is a Y/Z swap, as Sisir's exporter does it.
			strand.push_back(x);
			strand.push_back(o.unrealOrientation ? z : y);
			strand.push_back(o.unrealOrientation ? y : z);
		}
		if (!finite) continue;
		set.positions.insert(set.positions.end(), strand.begin(), strand.end());
		set.pointCounts.push_back((int32_t)count);
		if (o.rootUv) {
			// Already in the DCC convention (V flipped) in the bake.
			set.rootUvs.push_back(b.rootUvs[s * 2u]);
			set.rootUvs.push_back(b.rootUvs[s * 2u + 1u]);
		}
		if (o.clumpId) set.clumpIds.push_back(b.clumpIds[s]);
		for (uint32_t i = first; i < first + count; i++) {
			if (o.thickness) {
				const float w = (float)(b.widths[i] * widthScale);
				set.widths.push_back(std::isfinite(w) ? w : 0.0f);
			}
			if (o.color) set.colors.insert(set.colors.end(), color, color + 3);
			if (o.roughness) set.roughnesses.push_back(roughness);
			if (o.ao) set.aos.push_back(ao);
		}
	}
	return set;
}

// Alembic opens its file with the process code page, which in Maya is not UTF-8. A path it cannot
// spell is written under the folder's 8.3 name (or the temp folder) and then moved into place.
bool isAscii(const QString& s) {
	for (QChar c : s) if (c.unicode() > 0x7f) return false;
	return true;
}

QString shortPath(const QString& dir) {
	const std::wstring w = QDir::toNativeSeparators(dir).toStdWString();
	const DWORD n = GetShortPathNameW(w.c_str(), nullptr, 0);
	if (n == 0) return {};
	std::wstring out(n, L'\0');
	const DWORD m = GetShortPathNameW(w.c_str(), out.data(), n);
	if (m == 0 || m >= n) return {};
	out.resize(m);
	return QString::fromStdWString(out);
}

QString writablePathFor(const QString& target) {
	if (isAscii(target)) return target;
	const QString name = QString("sisir_abc_%1.tmp").arg(GetCurrentProcessId());
	for (const QString& dir : { QFileInfo(target).absolutePath(), QDir::tempPath() }) {
		const QString s = shortPath(dir);
		if (!s.isEmpty() && isAscii(s)) return QDir(s).filePath(name);
	}
	return {};
}

void showError(const QString& message, bool interactive) {
	MGlobal::displayError(toM(message));
	if (interactive) {
		MGlobal::executeCommandOnIdle(toM("confirmDialog -title \"Export Hair (ABC)\" -button \"OK\" -message " +
		                                  melQuote(message)), false);
	}
}

} // namespace

MSyntax ExportAbcCommand::newSyntax() {
	MSyntax syntax;
	syntax.addFlag("-f", "-file", MSyntax::kString);
	syntax.addFlag("-o", "-options", MSyntax::kString);
	syntax.addFlag("-nd", "-noDialog");
	syntax.addFlag("-c", "-check");
	syntax.setObjectType(MSyntax::kSelectionList, 0);
	syntax.useSelectionAsDefault(false);
	syntax.enableQuery(false);
	syntax.enableEdit(false);
	return syntax;
}

MStatus ExportAbcCommand::doIt(const MArgList& args) {
	MStatus status;
	MArgDatabase db(syntax(), args, &status);
	if (!status) return status;
	const bool interactive = MGlobal::mayaState() == MGlobal::kInteractive;

	MSelectionList given;
	db.getObjects(given);
	const std::vector<MDagPath> targets = resolveTargets(given);
	// -check: how many hair shapes the selection would export, without exporting -- the menu asks
	// before its file dialog, so an empty selection is reported first.
	if (db.isFlagSet("-check")) {
		setResult((int)targets.size());
		return MS::kSuccess;
	}

	MString value;
	if (!db.isFlagSet("-file") || !db.getFlagArgument("-file", 0, value) || value.length() == 0) {
		displayError("sisirExportAbc: -file is required");
		return MS::kInvalidParameter;
	}
	QString file = QDir::fromNativeSeparators(toQ(value));
	if (!file.endsWith(".abc", Qt::CaseInsensitive)) file += ".abc";

	if (targets.empty()) {
		showError(kSelectHair, interactive);
		return MS::kFailure;
	}

	float scale = 100.0f;
	ExportAbc::Options options;
	AbcExportDialog::loadRemembered(scale, options);
	if (db.isFlagSet("-options") && db.getFlagArgument("-options", 0, value)) {
		QString error;
		if (!applyOptionString(toQ(value), scale, options, &error)) {
			displayError(toM("sisirExportAbc: " + error));
			return MS::kInvalidParameter;
		}
	}

	if (interactive && !db.isFlagSet("-noDialog")) {
		// Sisir's own dialogs run modal loops, so not inside this command: from Maya's event loop
		// once the command has returned, then the export runs as a command of its own.
		QStringList names;
		for (const MDagPath& p : targets) names << melQuote(toQ(p.fullPathName()));
		const int maxPoints = maxPointsOf(targets);
		QTimer::singleShot(0, qApp, [file, names, maxPoints]() {
			QWidget host(MQtUtil::mainWindow());
			host.setAttribute(Qt::WA_WindowPropagation);
			host.hide();
			applySisirLook(&host);
			float chosenScale = 100.0f;
			ExportAbc::Options chosen;
			if (!AbcExportDialog::show(&host, chosenScale, chosen)) return;
			if (maxPoints > ExportAbc::kUnrealMaxPointsPerStrand &&
			    !AbcExportDialog::confirmUnrealPointLimit(&host, maxPoints)) return;
			const QString command = QString("sisirExportAbc -noDialog -file %1 -options %2 %3")
				.arg(melQuote(file), melQuote(optionString(chosenScale, chosen)), names.join(' '));
			MGlobal::executeCommand(toM(command), true, false);
		});
		return MS::kSuccess;
	}

	const QString writePath = writablePathFor(QDir::toNativeSeparators(file));
	if (writePath.isEmpty()) {
		showError("Cannot write to " + QDir::toNativeSeparators(file) + ": choose a folder whose path has no special characters.", interactive);
		return MS::kFailure;
	}
	QDir().mkpath(QFileInfo(file).absolutePath());

	uint32_t strands = 0;
	int sets = 0;
	{
		ExportAbc::Writer writer;
		std::string error;
		if (!writer.open(writePath.toStdString(), &error)) {
			showError("Cannot write " + QDir::toNativeSeparators(file) + ": " + QString::fromStdString(error), interactive);
			return MS::kFailure;
		}
		for (size_t i = 0; i < targets.size(); i++) {
			HairShape* shape = hairShapeOf(targets[i]);
			auto b = shape ? shape->buffers() : nullptr;
			if (!b || b->strandCount() == 0) continue;
			const ExportAbc::CurveSet set = curveSetOf(targets[i], *b, scale, options);
			if (set.pointCounts.empty()) continue;
			std::vector<std::string> warnings;
			if (!writer.addCurveSet(set, i, options, &warnings, &error)) {
				MGlobal::displayWarning(toM("Sisir: " + toQ(targets[i].partialPathName()) + " skipped: " + QString::fromStdString(error)));
				continue;
			}
			for (const std::string& w : warnings) {
				MGlobal::displayWarning(toM("Sisir: " + toQ(targets[i].partialPathName()) + ": " + QString::fromStdString(w)));
			}
			strands += (uint32_t)set.pointCounts.size();
			sets++;
		}
	}
	if (writePath != QDir::toNativeSeparators(file)) {
		const bool moved = MoveFileExW(writePath.toStdWString().c_str(), QDir::toNativeSeparators(file).toStdWString().c_str(),
		                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH) != 0;
		if (!moved) {
			DeleteFileW(writePath.toStdWString().c_str());
			showError("Cannot write " + QDir::toNativeSeparators(file), interactive);
			return MS::kFailure;
		}
	}
	if (sets == 0) {
		QFile::remove(file);
		showError("No hair was exported.", interactive);
		return MS::kFailure;
	}
	if (!QFileInfo::exists(file)) {
		showError("Cannot write " + QDir::toNativeSeparators(file), interactive);
		return MS::kFailure;
	}
	MGlobal::displayInfo(toM(QString("Sisir: exported %1 hair set%2 (%3 strands) to %4")
		.arg(sets).arg(sets == 1 ? "" : "s").arg(strands).arg(QDir::toNativeSeparators(file))));
	setResult(toM(file));
	return MS::kSuccess;
}

} // namespace SisirMaya
