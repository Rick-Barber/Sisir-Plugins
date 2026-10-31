// SisirBridge.exe: the Qt half of Sisir for Blender.
//
// Blender has no Qt, so the parts Sisir for Maya and Sisir for 3ds Max run inside the host -- the
// import dialog, the bake through Sisir.exe and its cache, the Export Hair (ABC) dialog and the
// Alembic writer -- run here, compiled from the very same sources (plugins/common, src/). The
// add-on (plugins/blender/sisir_for_blender) starts this next to the Sisir.exe it found and talks
// to it over stdin/stdout in UTF-8 lines; Blender builds the scene itself.
//
//   SisirBridge import <in.sir> [--owner <hwnd>] [--options <s>] [--sisir <exe>] [--assets <dir>]
//     Shows the import dialog over Blender (owned by <hwnd>) while Sisir bakes behind it.
//     out: PROGRESS <f> <label>   while baking
//          IMPORT\t<bake>\t<assets>\t<options>   the user pressed Import; the dialog shows the build
//          CANCEL                 the user cancelled (before Import, or Esc during the build)
//          ERROR\t<message>       the bake failed (the dialog already said so)
//     in:  PROGRESS <f> <label>   the build's progress, shown in the dialog
//          DONE | FAIL\t<message> | CANCELLED   the build is over: the dialog closes
//   SisirBridge bake <in.sir> [--assets <dir>] [--sisir <exe>]
//     The same bake without a dialog. out: PROGRESS lines, then RESULT\t<bake>\t<assets> or
//     ERROR\t<message>. A CANCEL line on stdin stops it.
//   SisirBridge export-abc <in.bin> <out.abc> [--options <s>] [--no-dialog] [--owner <hwnd>]
//     Sisir's Export Hair (ABC) dialog and writer over the hair Blender dumped (see readDump).
//     out: RESULT\t<file>\t<sets>\t<strands> | CANCEL | ERROR\t<message>, WARNING\t<message> lines.
//     Its choices are Sisir for Blender's own (metres to begin with), not Sisir's shared ones.
//   SisirBridge ply-units [--owner <hwnd>]
//     Export Guides (PLY)'s unit popup. out: RESULT\t<mm|cm|m> | CANCEL
//
// Every run starts by printing "SISIR_BRIDGE <protocol>", so an add-on can tell a bridge it does not
// speak from a broken one. Closing stdin (Blender gone) ends any run.

#include "AbcExportDialog.h"
#include "Baker.h"
#include "ExportAbcWriter.h"
#include "HostInfo.h"
#include "ImportDialog.h"
#include "PlyExportDialog.h"
#include "SirBakeReader.h"

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QTimer>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

constexpr int kProtocol = 1;

using SisirDcc::BakeJob;
using SisirDcc::BakeResult;
using SisirDcc::Baker;
using SisirDcc::ImportOptions;

void send(const QString& line) {
	const QByteArray utf8 = line.toUtf8() + '\n';
	std::fwrite(utf8.constData(), 1, (size_t)utf8.size(), stdout);
	std::fflush(stdout);
}

QString field(const QString& s) {
	// Tabs and newlines separate fields and lines; nothing Sisir names or paths should hold them.
	QString out = s;
	return out.replace('\t', ' ').replace('\n', ' ').replace('\r', ' ');
}

struct Args {
	QStringList positional;
	QString owner, options, sisir, assets;
	bool noDialog = false;
};

Args parseArgs(const QStringList& all) {
	Args a;
	for (int i = 2; i < all.size(); i++) {
		const QString& s = all[i];
		auto value = [&](QString& out) {
			if (i + 1 < all.size()) out = all[++i];
		};
		if (s == "--owner") value(a.owner);
		else if (s == "--options") value(a.options);
		else if (s == "--sisir") value(a.sisir);
		else if (s == "--assets") value(a.assets);
		else if (s == "--no-dialog") a.noDialog = true;
		else a.positional << s;
	}
	return a;
}

// Lines from Blender, read on their own thread (a pipe cannot be watched from Qt's event loop on
// Windows) and handed to `onLine` on the main thread. End of input arrives as an empty QString.
void readStdin(QObject* receiver, std::function<void(const QString&)> onLine) {
	std::thread([receiver = QPointer<QObject>(receiver), onLine = std::move(onLine)] {
		std::string line;
		while (std::getline(std::cin, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			const QString text = QString::fromUtf8(line.c_str()).trimmed();
			if (text.isEmpty()) continue;
			if (receiver) QMetaObject::invokeMethod(receiver, [onLine, text] { onLine(text); }, Qt::QueuedConnection);
		}
		if (receiver) QMetaObject::invokeMethod(receiver, [onLine] { onLine(QString()); }, Qt::QueuedConnection);
	}).detach();
}

HWND ownerWindow(const QString& text) {
	bool ok = false;
	const qulonglong v = text.toULongLong(&ok);
	HWND h = ok ? reinterpret_cast<HWND>(static_cast<uintptr_t>(v)) : nullptr;
	return h && IsWindow(h) ? h : nullptr;
}

// Blender's window as the owner of every dialog here, with Win32's modal rules: disabled while one
// is up and enabled again *before* it hides. Windows activates the owner of a window that hides
// only if the owner is enabled at that moment; otherwise it activates whatever app is next in
// z-order (VS Code, say) and Blender ends up behind it. The add-on also disables Blender's windows
// while the bridge runs and enables them after, but too late for that. WM_SHOWWINDOW comes before
// the change, for every way a dialog closes (Import, Cancel, Esc, an error, the ABC dialogs).
class OwnerModality : public QAbstractNativeEventFilter {
public:
	HWND owner = nullptr; // Blender
	HWND host = nullptr;  // the hidden widget Qt parents the ABC dialogs to: they go to Blender

	bool nativeEventFilter(const QByteArray& type, void* message, qintptr*) override {
		if (!owner || type != "windows_generic_MSG") return false;
		const MSG* m = static_cast<const MSG*>(message);
		if (m->message != WM_SHOWWINDOW || GetAncestor(m->hwnd, GA_ROOT) != m->hwnd) return false;
		HWND o = GetWindow(m->hwnd, GW_OWNER);
		if (m->wParam) {
			if (host && o == host) {
				SetWindowLongPtrW(m->hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
				o = owner;
			}
			if (o == owner) EnableWindow(owner, FALSE);
		} else if (o == owner) {
			EnableWindow(owner, TRUE);
		}
		return false;
	}
};

OwnerModality& modality() {
	static OwnerModality filter;
	return filter;
}

// Keeps `w` above Blender's window and centred on it, as a dialog Blender opened would be.
void ownBy(QWidget* w, HWND owner) {
	if (!owner) return;
	const HWND self = reinterpret_cast<HWND>(w->winId());
	SetWindowLongPtrW(self, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
	RECT r{};
	if (GetWindowRect(owner, &r)) {
		const qreal dpr = w->screen() ? w->screen()->devicePixelRatio() : 1.0;
		const QPoint centre((int)((r.left + r.right) / 2 / dpr), (int)((r.top + r.bottom) / 2 / dpr));
		w->move(centre - QPoint(w->width() / 2, w->height() / 2));
	}
}

void raise(QWidget* w) {
	w->raise();
	w->activateWindow();
	SetForegroundWindow(reinterpret_cast<HWND>(w->winId()));
}

void errorBox(QWidget* parent, const QString& title, const QString& text) {
	QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::Ok, parent);
	SisirDcc::applySisirLook(&box);
	box.exec();
}

// Sisir.exe as the plug-ins find it, asking the user where it is when it cannot be found.
QString locateSisir(const QString& explicitPath, bool interactive, QString* error) {
	QString exe = Baker::findSisir(explicitPath);
	if (exe.isEmpty() && interactive) {
		QMessageBox box(QMessageBox::Information, "Sisir Import", "Sisir was not found on this computer. Show where Sisir.exe is.");
		SisirDcc::applySisirLook(&box);
		box.exec();
		exe = QDir::toNativeSeparators(QFileDialog::getOpenFileName(nullptr, "Locate Sisir.exe", QString(), "Sisir (Sisir.exe)"));
		if (exe.isEmpty()) {
			*error = QString();
			return {};
		}
	}
	if (exe.isEmpty()) {
		*error = "Sisir was not found. Install Sisir, run it once, or pass its location with sisir=.";
		return {};
	}
	QString why;
	if (!Baker::canBake(exe, &why)) {
		*error = why;
		return {};
	}
	Baker::rememberSisir(exe);
	return exe;
}

bool sameDir(const QString& a, const QString& b) {
	return QDir::cleanPath(QFileInfo(a).absoluteFilePath()).compare(QDir::cleanPath(QFileInfo(b).absoluteFilePath()), Qt::CaseInsensitive) == 0;
}

// The bake copied the scene's meshes and textures into `from` before the user could pick another
// texture folder in the dialog: they go where the user asked.
bool copyAssets(const QString& from, const QString& to, QString* error) {
	const QDir src(from);
	if (!QDir().mkpath(to)) {
		*error = "cannot create " + QDir::toNativeSeparators(to);
		return false;
	}
	QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		const QString file = it.next();
		const QString target = QDir(to).filePath(src.relativeFilePath(file));
		QDir().mkpath(QFileInfo(target).absolutePath());
		QFile::remove(target);
		if (!QFile::copy(file, target)) {
			*error = "cannot copy textures to " + QDir::toNativeSeparators(to);
			return false;
		}
	}
	return true;
}

// The import dialog, with Esc during the build reporting a cancel instead of being ignored (Max's
// importer polls Esc the same way while it builds).
class BridgeDialog : public SisirDcc::ImportDialog {
public:
	using ImportDialog::ImportDialog;
	std::function<void()> onBuildCancel;
	void reject() override {
		if (building()) {
			if (onBuildCancel) onBuildCancel();
			return;
		}
		ImportDialog::reject();
	}
};

int runImport(const Args& args) {
	if (args.positional.isEmpty()) {
		send("ERROR\tusage: SisirBridge import <in.sir>");
		return 2;
	}
	const QFileInfo sir(args.positional.first());
	ImportOptions options = ImportOptions::fromString(args.options, ImportOptions::loadRemembered());
	// --sisir is the Sisir this bridge ships with (the add-on found the bridge next to it): it bakes,
	// but only an explicit sisir= in the options is remembered, as in the other plug-ins.
	const QString sisirOverride = options.sisirExecutable;
	options.sisirExecutable = args.sisir.isEmpty() ? sisirOverride : args.sisir;
	if (!sir.isFile()) {
		errorBox(nullptr, "Sisir Import", "file not found: " + QDir::toNativeSeparators(sir.filePath()));
		send("ERROR\tfile not found: " + field(QDir::toNativeSeparators(sir.filePath())));
		return 1;
	}
	QString error;
	const QString exe = locateSisir(options.sisirExecutable, true, &error);
	if (exe.isEmpty()) {
		if (error.isEmpty()) {
			send("CANCEL");
			return 0;
		}
		errorBox(nullptr, "Sisir Import", error);
		send("ERROR\t" + field(error));
		return 1;
	}
	const QString source = sir.absoluteFilePath();
	QString assets = options.textureFolder.isEmpty() ? args.assets : options.textureFolder;
	if (assets.isEmpty()) assets = QDir(QDir::tempPath()).filePath("sisir/" + sir.completeBaseName());

	BridgeDialog dialog(options, source, nullptr);
	dialog.winId();
	ownBy(&dialog, ownerWindow(args.owner));
	BakeResult bake;
	bool importing = false, finished = false, cancelSent = false;
	auto cancel = [&] {
		if (cancelSent) return;
		cancelSent = true;
		send("CANCEL");
	};
	dialog.onBuildCancel = cancel;

	auto* job = new BakeJob(&dialog); // closing the dialog stops Sisir
	QObject::connect(job, &BakeJob::progress, &dialog, [&dialog](float f, const QString& label) {
		dialog.setProgress(f, label);
		send(QString("PROGRESS %1 %2").arg(f, 0, 'f', 3).arg(field(label)));
	});
	QObject::connect(job, &BakeJob::finished, &dialog, [&](const BakeResult& result) {
		bake = result;
		SirBake::Reader reader;
		QString why = result.error;
		if (result.ok && reader.open(result.bakeFile, &why)) {
			dialog.setMeta(reader.meta());
			// tools/blender_gui_smoke.py: press Import unattended (SISIR_BRIDGE_TEST=accept|cancel-build).
			if (!qEnvironmentVariableIsEmpty("SISIR_BRIDGE_TEST")) {
				QTimer::singleShot(200, &dialog, [&dialog] {
					if (auto* button = dialog.findChild<QPushButton*>("primary")) button->click();
				});
			}
			return;
		}
		finished = true;
		errorBox(&dialog, "Sisir Import", why);
		send("ERROR\t" + field(why));
		dialog.done(QDialog::Rejected);
	});
	QObject::connect(&dialog, &SisirDcc::ImportDialog::importRequested, &dialog, [&] {
		ImportOptions chosen = dialog.options();
		chosen.sisirExecutable = sisirOverride;
		chosen.remember();
		QString assetsDir = bake.assetsDir;
		if (!chosen.textureFolder.isEmpty() && !sameDir(chosen.textureFolder, assetsDir)) {
			QString why;
			if (!copyAssets(assetsDir, chosen.textureFolder, &why)) {
				errorBox(&dialog, "Sisir Import", why);
				return;
			}
			assetsDir = chosen.textureFolder;
		}
		importing = true;
		dialog.setBuilding();
		send("IMPORT\t" + field(QDir::toNativeSeparators(bake.bakeFile)) + "\t" + field(QDir::toNativeSeparators(assetsDir)) +
		     "\t" + field(chosen.toString()));
	});
	readStdin(&dialog, [&](const QString& line) {
		if (line.isEmpty()) { // Blender is gone
			finished = true;
			if (dialog.building()) dialog.finishBuilding();
			else dialog.done(QDialog::Rejected);
			return;
		}
		if (!importing) return;
		if (line.startsWith("PROGRESS ")) {
			const QString rest = line.mid(9);
			const int space = rest.indexOf(' ');
			const float f = (space < 0 ? rest : rest.left(space)).toFloat();
			dialog.showBuildProgress(f, space < 0 ? QString() : rest.mid(space + 1));
			if (qEnvironmentVariable("SISIR_BRIDGE_TEST") == "cancel-build") dialog.reject(); // as Esc would
		} else if (line == "DONE" || line == "CANCELLED" || line.startsWith("FAIL")) {
			finished = true;
			if (line.startsWith("FAIL")) {
				const QString why = line.section('\t', 1);
				if (!why.isEmpty()) errorBox(&dialog, "Sisir Import", why);
			}
			dialog.finishBuilding();
		}
	});
	job->start(source, assets, exe);
	dialog.show();
	raise(&dialog);
	dialog.exec();
	if (!importing && !finished) cancel();
	return 0;
}

int runBake(const Args& args) {
	if (args.positional.isEmpty()) {
		send("ERROR\tusage: SisirBridge bake <in.sir>");
		return 2;
	}
	const QFileInfo sir(args.positional.first());
	QString error;
	const QString exe = locateSisir(args.sisir, false, &error);
	if (exe.isEmpty()) {
		send("ERROR\t" + field(error));
		return 1;
	}
	const QString assets = args.assets.isEmpty() ? QDir(QDir::tempPath()).filePath("sisir/" + sir.completeBaseName()) : args.assets;
	static std::atomic<bool> cancelled{ false };
	QObject stdinReceiver;
	readStdin(&stdinReceiver, [](const QString& line) {
		if (line.isEmpty() || line == "CANCEL") cancelled = true;
	});
	const BakeResult bake = Baker::bake(sir.absoluteFilePath(), assets, exe, [](float f, const QString& label) {
		QCoreApplication::processEvents(); // delivers the stdin lines
		if (f >= 0.0f) send(QString("PROGRESS %1 %2").arg(f, 0, 'f', 3).arg(field(label)));
		return !cancelled.load();
	});
	if (!bake.ok) {
		send(bake.cancelled ? QString("CANCEL") : "ERROR\t" + field(bake.error));
		return bake.cancelled ? 0 : 1;
	}
	send("RESULT\t" + field(QDir::toNativeSeparators(bake.bakeFile)) + "\t" + field(QDir::toNativeSeparators(bake.assetsDir)));
	return 0;
}

// --- Export Hair (ABC) ---------------------------------------------------------------------------

// What Blender dumps (plugins/blender/sisir_for_blender/export_abc.py), little-endian, no padding:
//   "SISIRABC" u32 version=1 u32 setCount u32 maxPointsPerStrand
//   per set: u32 nameBytes, name (UTF-8), u32 strands, u32 points,
//            i32 pointCounts[strands], f32 positions[points*3] (metres, Sisir's Y-up, world),
//            f32 widths[points] (metres, full width, scaled as drawn), f32 rootUvs[strands*2],
//            i32 clumpIds[strands], f32 color[3], f32 roughness, f32 ao
struct DumpSet {
	std::string name;
	std::vector<int32_t> pointCounts;
	std::vector<float> positions, widths, rootUvs;
	std::vector<int32_t> clumpIds;
	float color[3] = { 0.9f, 0.8f, 0.65f };
	float roughness = 0.05f, ao = 1.0f;
};

bool readDump(const QString& path, std::vector<DumpSet>& sets, int& maxPoints, QString* error) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		*error = "cannot read " + QDir::toNativeSeparators(path);
		return false;
	}
	const QByteArray data = f.readAll();
	size_t pos = 0;
	auto take = [&](void* out, size_t bytes) {
		if (pos + bytes > (size_t)data.size()) return false;
		if (bytes) std::memcpy(out, data.constData() + pos, bytes);
		pos += bytes;
		return true;
	};
	char magic[8];
	uint32_t version = 0, count = 0, longest = 0;
	if (!take(magic, 8) || std::memcmp(magic, "SISIRABC", 8) != 0 || !take(&version, 4) || version != 1 || !take(&count, 4) ||
	    !take(&longest, 4)) {
		*error = "the hair dump is not readable";
		return false;
	}
	maxPoints = (int)longest;
	sets.resize(count);
	for (DumpSet& s : sets) {
		uint32_t nameBytes = 0, strands = 0, points = 0;
		if (!take(&nameBytes, 4) || nameBytes > 4096) return *error = "the hair dump is truncated", false;
		s.name.resize(nameBytes);
		if (!take(s.name.data(), nameBytes) || !take(&strands, 4) || !take(&points, 4)) return *error = "the hair dump is truncated", false;
		s.pointCounts.resize(strands);
		s.positions.resize((size_t)points * 3);
		s.widths.resize(points);
		s.rootUvs.resize((size_t)strands * 2);
		s.clumpIds.resize(strands);
		if (!take(s.pointCounts.data(), strands * 4ull) || !take(s.positions.data(), points * 12ull) || !take(s.widths.data(), points * 4ull) ||
		    !take(s.rootUvs.data(), strands * 8ull) || !take(s.clumpIds.data(), strands * 4ull) || !take(s.color, 12) ||
		    !take(&s.roughness, 4) || !take(&s.ao, 4)) {
			*error = "the hair dump is truncated";
			return false;
		}
		uint64_t sum = 0;
		for (int32_t c : s.pointCounts) sum += (uint64_t)std::max(c, 0);
		if (sum != points) {
			*error = "the hair dump is inconsistent";
			return false;
		}
	}
	return true;
}

// "units=cm;color=1;unreal=0" over `scale` and `o`, as sisir.exportAbc takes it in 3ds Max.
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

// One dumped set as the writer's CurveSet, the way Sisir for 3ds Max builds it (ExportAbc.cpp).
ExportAbc::CurveSet curveSetOf(const DumpSet& d, float scale, const ExportAbc::Options& o) {
	ExportAbc::CurveSet set;
	set.name = d.name;
	const float color[3] = { std::clamp(d.color[0], 0.0f, 1.0f), std::clamp(d.color[1], 0.0f, 1.0f), std::clamp(d.color[2], 0.0f, 1.0f) };
	const float roughness = std::clamp(d.roughness, 0.0f, 1.0f), ao = std::clamp(d.ao, 0.0f, 1.0f);
	std::vector<float> strand;
	size_t first = 0;
	for (size_t s = 0; s < d.pointCounts.size(); s++) {
		const size_t count = (size_t)std::max(d.pointCounts[s], 0);
		strand.clear();
		bool finite = count >= 2;
		for (size_t i = first; i < first + count && finite; i++) {
			const float x0 = d.positions[i * 3] * scale, x1 = d.positions[i * 3 + 1] * scale, x2 = d.positions[i * 3 + 2] * scale;
			finite = std::isfinite(x0) && std::isfinite(x1) && std::isfinite(x2);
			strand.push_back(x0);
			strand.push_back(o.unrealOrientation ? x2 : x1);
			strand.push_back(o.unrealOrientation ? x1 : x2);
		}
		if (finite) {
			set.positions.insert(set.positions.end(), strand.begin(), strand.end());
			set.pointCounts.push_back((int32_t)count);
			if (o.rootUv) {
				set.rootUvs.push_back(d.rootUvs[s * 2]);
				set.rootUvs.push_back(d.rootUvs[s * 2 + 1]);
			}
			if (o.clumpId) set.clumpIds.push_back(d.clumpIds[s]);
			for (size_t i = first; i < first + count; i++) {
				if (o.thickness) {
					const float w = d.widths[i] * scale;
					set.widths.push_back(std::isfinite(w) ? w : 0.0f);
				}
				if (o.color) set.colors.insert(set.colors.end(), color, color + 3);
				if (o.roughness) set.roughnesses.push_back(roughness);
				if (o.ao) set.aos.push_back(ao);
			}
		}
		first += count;
	}
	return set;
}

// The key Sisir for Blender keeps its own Export Hair (ABC) choices under (see below).
constexpr const wchar_t* kBlenderAbcRoot = L"Software\\Sisir\\SisirForBlender\\AbcExport";

// Sisir's Export Hair (ABC) dialog remembers its choices in Sisir's own settings (QSettings("Sisir",
// "Sisir"), the registry), which Sisir and Sisir for Maya and 3ds Max share, centimetres by default.
// Blender works in metres: in this process only, HKEY_CURRENT_USER is mapped to a key of Sisir for
// Blender's own (RegOverridePredefKey), so the same dialog reads and saves Blender's choices there --
// first seeded with Meters -- and never changes Sisir's or the other plug-ins'.
void useBlendersOwnAbcSettings() {
	HKEY root = nullptr;
	if (RegCreateKeyExW(HKEY_CURRENT_USER, kBlenderAbcRoot, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &root, nullptr) != ERROR_SUCCESS) return;
	if (RegOverridePredefKey(HKEY_CURRENT_USER, root) != ERROR_SUCCESS) {
		RegCloseKey(root);
		return;
	}
	RegCloseKey(root);
	QSettings settings("Sisir", "Sisir");
	if (!settings.contains("qt/abcExport/units")) settings.setValue("qt/abcExport/units", 2); // Meters (m)
}

// Export Guides (PLY)'s unit popup (plugins/common/PlyExportDialog), the one Sisir for Maya and
// 3ds Max show, over Blender.
int runPlyUnits(const Args& args) {
	QWidget host;
	host.setAttribute(Qt::WA_WindowPropagation);
	SisirDcc::applySisirLook(&host);
	host.winId();
	ownBy(&host, ownerWindow(args.owner));
	modality().host = reinterpret_cast<HWND>(host.winId());
	QTimer::singleShot(0, &host, [] {
		if (QWidget* w = QApplication::activeModalWidget()) raise(w);
	});
	// tools/blender_gui_smoke.py: press Export unattended.
	QTimer autoExport;
	if (!qEnvironmentVariableIsEmpty("SISIR_BRIDGE_TEST")) {
		QObject::connect(&autoExport, &QTimer::timeout, [] {
			QWidget* w = QApplication::activeModalWidget();
			for (QPushButton* b : w ? w->findChildren<QPushButton*>() : QList<QPushButton*>()) {
				if (b->text() == "Export") b->click();
			}
		});
		autoExport.start(300);
	}
	const QString unit = SisirDcc::askPlyUnit(&host);
	send(unit.isEmpty() ? QString("CANCEL") : "RESULT\t" + unit);
	return 0;
}

int runExportAbc(const Args& args) {
	if (args.positional.size() < 2) {
		send("ERROR\tusage: SisirBridge export-abc <in.bin> <out.abc>");
		return 2;
	}
	const bool interactive = !args.noDialog;
	auto fail = [&](const QString& message) {
		if (interactive) errorBox(nullptr, "Export Hair (ABC)", message);
		send("ERROR\t" + field(message));
		return 1;
	};
	std::vector<DumpSet> sets;
	int maxPoints = 0;
	QString error;
	if (!readDump(args.positional[0], sets, maxPoints, &error)) return fail(error);
	QString file = QDir::fromNativeSeparators(args.positional[1]);
	if (!file.endsWith(".abc", Qt::CaseInsensitive)) file += ".abc";

	useBlendersOwnAbcSettings();
	float scale = 100.0f;
	ExportAbc::Options options;
	AbcExportDialog::loadRemembered(scale, options);
	if (!args.options.isEmpty() && !applyOptionString(args.options, scale, options, &error)) {
		send("ERROR\t" + field(error));
		return 1;
	}
	if (interactive) {
		// A hidden owner carrying Sisir's look, which the dialogs inherit (as in 3ds Max).
		QWidget host;
		host.setAttribute(Qt::WA_WindowPropagation);
		SisirDcc::applySisirLook(&host);
		host.winId();
		ownBy(&host, ownerWindow(args.owner));
		modality().host = reinterpret_cast<HWND>(host.winId());
		QTimer::singleShot(0, &host, [] {
			if (QWidget* w = QApplication::activeModalWidget()) raise(w);
		});
		// tools/blender_gui_smoke.py: press Export unattended (the remembered choices stay as they were).
		QTimer autoExport;
		if (!qEnvironmentVariableIsEmpty("SISIR_BRIDGE_TEST")) {
			QObject::connect(&autoExport, &QTimer::timeout, [] {
				QWidget* w = QApplication::activeModalWidget();
				for (QPushButton* b : w ? w->findChildren<QPushButton*>() : QList<QPushButton*>()) {
					if (b->text() == "Export" || b->text() == "Continue Export") b->click();
				}
			});
			autoExport.start(300);
		}
		if (!AbcExportDialog::show(&host, scale, options)) {
			send("CANCEL");
			return 0;
		}
		if (maxPoints > ExportAbc::kUnrealMaxPointsPerStrand && !AbcExportDialog::confirmUnrealPointLimit(&host, maxPoints)) {
			send("CANCEL");
			return 0;
		}
	}

	QDir().mkpath(QFileInfo(file).absolutePath());
	uint32_t strands = 0;
	int written = 0;
	{
		// This process's code page is UTF-8 (SisirBridge.manifest), so Alembic opens any path.
		ExportAbc::Writer writer;
		std::string why;
		if (!writer.open(QDir::toNativeSeparators(file).toStdString(), &why)) {
			return fail("Cannot write " + QDir::toNativeSeparators(file) + ": " + QString::fromStdString(why));
		}
		for (size_t i = 0; i < sets.size(); i++) {
			const ExportAbc::CurveSet set = curveSetOf(sets[i], scale, options);
			if (set.pointCounts.empty()) continue;
			std::vector<std::string> warnings;
			const QString name = QString::fromStdString(sets[i].name);
			if (!writer.addCurveSet(set, i, options, &warnings, &why)) {
				send("WARNING\t" + field(name + " skipped: " + QString::fromStdString(why)));
				continue;
			}
			for (const std::string& w : warnings) send("WARNING\t" + field(name + ": " + QString::fromStdString(w)));
			strands += (uint32_t)set.pointCounts.size();
			written++;
		}
	}
	if (written == 0) {
		QFile::remove(file);
		return fail("No hair was exported.");
	}
	if (!QFileInfo::exists(file)) return fail("Cannot write " + QDir::toNativeSeparators(file));
	send(QString("RESULT\t%1\t%2\t%3").arg(field(QDir::toNativeSeparators(file))).arg(written).arg(strands));
	return 0;
}

} // namespace

int main(int argc, char** argv) {
	// The dialogs come out the size Maya and 3ds Max show them at. Those hosts draw Qt with no pixel
	// scaling (device pixel ratio 1, measured in Maya 2025 at 150%: DPR 1.0, 144 DPI) and only the
	// point-size text follows the display's DPI; Qt 6 on its own would also blow every px size --
	// the window, buttons, stylesheet padding -- up by the display scale. The process stays DPI aware,
	// so text is still drawn sharp, not stretched. Only Qt's constructor reads the variable, so it is
	// put back afterwards and the Sisir.exe this process starts does not inherit it.
	const bool scalingPreset = qEnvironmentVariableIsSet("QT_ENABLE_HIGHDPI_SCALING");
	if (!scalingPreset) qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
	QApplication app(argc, argv);
	if (!scalingPreset) qunsetenv("QT_ENABLE_HIGHDPI_SCALING");
	QApplication::setQuitOnLastWindowClosed(false);
	SisirDcc::setHost({ "SisirForBlender", "Blender", "blender", "Next to the .blend file", "m" });
	send(QString("SISIR_BRIDGE %1").arg(kProtocol));
	const QStringList all = QCoreApplication::arguments();
	const QString command = all.value(1);
	const Args args = parseArgs(all);
	modality().owner = ownerWindow(args.owner);
	app.installNativeEventFilter(&modality());
	if (command == "import") return runImport(args);
	if (command == "bake") return runBake(args);
	if (command == "export-abc") return runExportAbc(args);
	if (command == "ply-units") return runPlyUnits(args);
	send("ERROR\tusage: SisirBridge import|bake|export-abc ...");
	return 2;
}
