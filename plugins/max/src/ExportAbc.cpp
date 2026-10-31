#include "ExportAbc.h"

#include "AbcExportDialog.h"
#include "ClassIds.h"
#include "ExportAbcWriter.h"
#include "HairObject.h"
#include "ImportDialog.h"
#include "MaxUtil.h"

#include <inode.h>
#include <Qt/QmaxMainWindow.h>

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QSet>
#include <QWidget>

#include <units.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace SisirMax {

namespace {

const QString kSelectHair = "Select the Sisir hair to export, or a group that contains it.";

HairObject* hairOf(INode* node, TimeValue t) {
	if (!node) return nullptr;
	Object* obj = node->EvalWorldState(t).obj;
	return obj && obj->ClassID() == kHairObjectClassId ? static_cast<HairObject*>(obj) : nullptr;
}

bool visibleInViewport(INode* node) {
	for (INode* n = node; n && !n->IsRootNode(); n = n->GetParentNode()) {
		if (n->IsNodeHidden(TRUE)) return false;
	}
	return true;
}

std::vector<INode*> resolveTargets() {
	Interface* ip = GetCOREInterface();
	const TimeValue t = ip->GetTime();
	std::vector<INode*> out;
	QSet<ULONG> seen;
	auto add = [&](INode* n) {
		if (seen.contains(n->GetHandle())) return;
		seen.insert(n->GetHandle());
		out.push_back(n);
	};
	std::function<void(INode*)> addUnder = [&](INode* n) {
		for (int c = 0; c < n->NumberOfChildren(); c++) {
			INode* child = n->GetChildNode(c);
			if (hairOf(child, t) && visibleInViewport(child)) add(child);
			addUnder(child);
		}
	};
	for (int i = 0; i < ip->GetSelNodeCount(); i++) {
		INode* n = ip->GetSelNode(i);
		if (hairOf(n, t)) {
			add(n);
			continue;
		}
		// A node straight above hair objects: those, visible or not.
		for (int c = 0; c < n->NumberOfChildren(); c++) {
			if (hairOf(n->GetChildNode(c), t)) add(n->GetChildNode(c));
		}
		addUnder(n);
	}
	return out;
}

int maxPointsOf(const std::vector<INode*>& targets) {
	const TimeValue t = GetCOREInterface()->GetTime();
	int maxPoints = 0;
	for (INode* n : targets) {
		HairObject* hair = hairOf(n, t);
		auto b = hair ? hair->buffers() : nullptr;
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

// One object as the writer's CurveSet: world space, Max system units to metres to the chosen unit,
// Z-up back to Sisir's Y-up, the way Sisir's exporter writes its metres.
ExportAbc::CurveSet curveSetOf(INode* node, HairObject& hair, const SisirDcc::HairBuffers& b, float scale, const ExportAbc::Options& o) {
	ExportAbc::CurveSet set;
	set.name = hair.paramString(kHairSetName).toStdString();
	if (set.name.empty()) set.name = toQ(node->GetName()).toStdString();
	const TimeValue t = GetCOREInterface()->GetTime();
	const Matrix3 world = node->GetObjectTM(t);
	// Metres per number: what the import recorded (its Units choice), else the system unit.
	MSTR recorded;
	double metresPerUnit = GetSystemUnitScale(UNITS_METERS);
	if (node->GetUserPropString(_M("sisir_metres_per_unit"), recorded)) {
		const double v = toQ(recorded).toDouble();
		if (v > 0.0) metresPerUnit = v;
	}
	const double toUnit = metresPerUnit * (double)scale;
	// Strand widths follow the transform's uniform scale, as the viewport and Arnold draw them.
	const Point3 r0 = world.GetRow(0), r1 = world.GetRow(1), r2 = world.GetRow(2);
	const double det = DotProd(r0, CrossProd(r1, r2));
	const double widthScale = std::cbrt(std::fabs(det)) * (double)hair.paramFloat(kWidthScale) * toUnit;
	const Point3 c = hair.paramColor(kSetColor);
	const float color[3] = { std::clamp(c.x, 0.0f, 1.0f), std::clamp(c.y, 0.0f, 1.0f), std::clamp(c.z, 0.0f, 1.0f) };
	const float roughness = std::clamp(hair.paramFloat(kGroomRoughness), 0.0f, 1.0f);
	const float ao = std::clamp(hair.paramFloat(kGroomAo), 0.0f, 1.0f);

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
			const Point3 p = Point3(b.positions[i * 3u], b.positions[i * 3u + 1u], b.positions[i * 3u + 2u]) * world;
			float y[3];
			maxToSisir(p, y);
			const float x0 = (float)(y[0] * toUnit), x1 = (float)(y[1] * toUnit), x2 = (float)(y[2] * toUnit);
			finite = std::isfinite(x0) && std::isfinite(x1) && std::isfinite(x2);
			// Y-up (Sisir) to Z-up (Unreal) is a Y/Z swap, as Sisir's exporter does it.
			strand.push_back(x0);
			strand.push_back(o.unrealOrientation ? x2 : x1);
			strand.push_back(o.unrealOrientation ? x1 : x2);
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

// Alembic opens its file with the process code page, which in Max is not UTF-8. A path it cannot
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

void fail(const QString& message) { showErrorLater("Export Hair (ABC)", message); }

} // namespace

int exportAbcTargetCount() { return (int)resolveTargets().size(); }

QString exportAbc(const QString& fileIn, const QString& optionText, bool showDialog) {
	const std::vector<INode*> targets = resolveTargets();
	if (fileIn.isEmpty()) {
		logError("sisir.exportAbc: a file is required");
		return {};
	}
	QString file = QDir::fromNativeSeparators(fileIn);
	if (!file.endsWith(".abc", Qt::CaseInsensitive)) file += ".abc";
	if (targets.empty()) {
		fail(kSelectHair);
		return {};
	}

	float scale = 100.0f;
	ExportAbc::Options options;
	AbcExportDialog::loadRemembered(scale, options);
	if (!optionText.isEmpty()) {
		QString error;
		if (!applyOptionString(optionText, scale, options, &error)) {
			logError("sisir.exportAbc: " + error);
			return {};
		}
	}
	if (showDialog && interactive()) {
		QWidget host(GetCOREInterface()->GetQmaxMainWindow());
		host.setAttribute(Qt::WA_WindowPropagation);
		host.hide();
		SisirDcc::applySisirLook(&host);
		if (!AbcExportDialog::show(&host, scale, options)) return {};
		const int maxPoints = maxPointsOf(targets);
		if (maxPoints > ExportAbc::kUnrealMaxPointsPerStrand && !AbcExportDialog::confirmUnrealPointLimit(&host, maxPoints)) return {};
	}

	const QString writePath = writablePathFor(QDir::toNativeSeparators(file));
	if (writePath.isEmpty()) {
		fail("Cannot write to " + QDir::toNativeSeparators(file) + ": choose a folder whose path has no special characters.");
		return {};
	}
	QDir().mkpath(QFileInfo(file).absolutePath());

	uint32_t strands = 0;
	int sets = 0;
	const TimeValue t = GetCOREInterface()->GetTime();
	{
		ExportAbc::Writer writer;
		std::string error;
		if (!writer.open(writePath.toStdString(), &error)) {
			fail("Cannot write " + QDir::toNativeSeparators(file) + ": " + QString::fromStdString(error));
			return {};
		}
		for (size_t i = 0; i < targets.size(); i++) {
			HairObject* hair = hairOf(targets[i], t);
			auto b = hair ? hair->buffers() : nullptr;
			if (!b || b->strandCount() == 0) continue;
			const ExportAbc::CurveSet set = curveSetOf(targets[i], *hair, *b, scale, options);
			if (set.pointCounts.empty()) continue;
			std::vector<std::string> warnings;
			const QString nodeName = toQ(targets[i]->GetName());
			if (!writer.addCurveSet(set, i, options, &warnings, &error)) {
				logWarning(nodeName + " skipped: " + QString::fromStdString(error));
				continue;
			}
			for (const std::string& w : warnings) logWarning(nodeName + ": " + QString::fromStdString(w));
			strands += (uint32_t)set.pointCounts.size();
			sets++;
		}
	}
	if (writePath != QDir::toNativeSeparators(file)) {
		const bool moved = MoveFileExW(writePath.toStdWString().c_str(), QDir::toNativeSeparators(file).toStdWString().c_str(),
		                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH) != 0;
		if (!moved) {
			DeleteFileW(writePath.toStdWString().c_str());
			fail("Cannot write " + QDir::toNativeSeparators(file));
			return {};
		}
	}
	if (sets == 0) {
		QFile::remove(file);
		fail("No hair was exported.");
		return {};
	}
	if (!QFileInfo::exists(file)) {
		fail("Cannot write " + QDir::toNativeSeparators(file));
		return {};
	}
	logInfo(QString("exported %1 hair set%2 (%3 strands) to %4")
	            .arg(sets).arg(sets == 1 ? "" : "s").arg(strands).arg(QDir::toNativeSeparators(file)));
	return file;
}

void exportAbcInteractive() {
	// Sisir's order: check the selection, where to, then the export dialog.
	if (exportAbcTargetCount() == 0) {
		MessageBoxW(GetCOREInterface()->GetMAXHWnd(), toM(kSelectHair).data(), L"Export Hair (ABC)", MB_OK | MB_ICONINFORMATION);
		return;
	}
	const QString file = QFileDialog::getSaveFileName(GetCOREInterface()->GetQmaxMainWindow(), "Export Hair", QString(),
	                                                  "Alembic Files (*.abc)");
	if (!file.isEmpty()) exportAbc(file, QString(), true);
}

} // namespace SisirMax
