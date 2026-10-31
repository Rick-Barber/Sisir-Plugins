#include "ImportFlow.h"

#include "Baker.h"
#include "ImportDialog.h"
#include "MaxUtil.h"
#include "SceneBuilder.h"
#include "SirBakeReader.h"

#include <hold.h>
#include <inode.h>
#include <IPathConfigMgr.h>
#include <Qt/QmaxMainWindow.h>

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLocale>

#include <algorithm>
#include <memory>

namespace SisirMax {

using SisirDcc::BakeJob;
using SisirDcc::BakeResult;
using SisirDcc::Baker;
using SisirDcc::ImportDialog;

namespace {

QWidget* maxWindow() { return GetCOREInterface()->GetQmaxMainWindow(); }

QString defaultTextureFolder(const QString& sceneName) {
	// The project's sceneassets/images, as Max's own bitmap paths default to.
	QString images = toQ(IPathConfigMgr::GetPathConfigMgr()->GetDir(APP_IMAGE_DIR));
	if (images.isEmpty()) images = QDir::home().filePath("Documents/3ds Max/sceneassets/images");
	return QDir(QDir::fromNativeSeparators(images)).filePath("sisir/" + legalName(sceneName));
}

void deleteNodes(const std::vector<ULONG>& handles) {
	Interface* ip = GetCOREInterface();
	// Children first, so no parent goes while it still holds one of ours.
	for (auto it = handles.rbegin(); it != handles.rend(); ++it) {
		if (INode* node = ip->GetINodeByHandle(*it)) ip->DeleteNode(node, FALSE);
	}
	ip->RedrawViews(ip->GetTime());
}

struct Built {
	bool ok = false;
	bool cancelled = false;
	QString error;
	QString rootName;
	std::vector<ULONG> handles;
};

Built buildScene(const QString& bakeFile, const QString& assetsDir, const SisirDcc::ImportOptions& options,
                 const QString& sourceFile, bool interactive, const SceneBuilder::Progress& progress) {
	Built out;
	SirBake::Reader reader;
	if (!reader.open(bakeFile, &out.error)) return out;
	SceneBuilder builder(reader, options, assetsDir, QDir::toNativeSeparators(sourceFile));
	BuildReport report;
	// The import records its own undo step (ImportRestore): nothing of the build itself goes on the
	// undo stack, so one Undo removes all of it.
	theHold.Suspend();
	const bool ok = builder.build(report, &out.error, progress);
	theHold.Resume();
	for (INode* n : builder.createdNodes()) out.handles.push_back(n->GetHandle());
	if (!ok) {
		theHold.Suspend();
		deleteNodes(out.handles);
		theHold.Resume();
		out.handles.clear();
		out.cancelled = out.error == "Import cancelled";
		return out;
	}
	for (const QString& w : report.warnings) logWarning(w);
	logInfo(QString("imported %1: %2 hair sets (%3 strands), %4 guide curves, %5 card layers, %6 meshes")
	            .arg(QFileInfo(sourceFile).completeBaseName()).arg(report.hairSets).arg(QLocale().toString(report.hairStrands))
	            .arg(report.guideCurves).arg(report.cardLayers).arg(report.meshes));
	out.ok = true;
	out.rootName = report.root ? toQ(report.root->GetName()) : QString();
	(void)interactive;
	return out;
}

// The import as one undo step: Undo deletes what it made, Redo builds it again from the cached
// bake with the same choices, without asking.
class ImportRestore : public RestoreObj {
public:
	ImportRestore(std::vector<ULONG> handles, QString bakeFile, QString assetsDir, SisirDcc::ImportOptions options, QString source)
		: m_handles(std::move(handles)), m_bakeFile(std::move(bakeFile)), m_assetsDir(std::move(assetsDir)),
		  m_options(std::move(options)), m_source(std::move(source)) {}

	void Restore(int) override {
		theHold.Suspend();
		deleteNodes(m_handles);
		theHold.Resume();
	}
	void Redo() override {
		const Built again = buildScene(m_bakeFile, m_assetsDir, m_options, m_source, false, {});
		if (again.ok) m_handles = again.handles;
		GetCOREInterface()->RedrawViews(GetCOREInterface()->GetTime());
	}
	int Size() override { return (int)(m_handles.size() * sizeof(ULONG)) + 256; }
	MSTR Description() override { return MSTR(_M("Import Sisir Scene")); }

private:
	std::vector<ULONG> m_handles;
	QString m_bakeFile, m_assetsDir;
	SisirDcc::ImportOptions m_options;
	QString m_source;
};

void recordUndo(const Built& built, const QString& bakeFile, const QString& assetsDir, const SisirDcc::ImportOptions& options,
                const QString& source) {
	auto* restore = new ImportRestore(built.handles, bakeFile, assetsDir, options, source);
	if (theHold.Holding()) {
		theHold.Put(restore);
		return;
	}
	theHold.Begin();
	theHold.Put(restore);
	theHold.Accept(_M("Import Sisir Scene"));
}

// Max's progress bar in the status line, for an import without the dialog.
class StatusProgress {
public:
	explicit StatusProgress(bool enabled) : m_enabled(enabled) {}
	~StatusProgress() { end(); }
	bool step(float fraction, const QString& label) {
		if (!m_enabled) return true;
		Interface* ip = GetCOREInterface();
		if (!m_started) {
			ip->ProgressStart(_M("Sisir Import"), TRUE, &StatusProgress::noWork, nullptr);
			m_started = true;
		}
		if (fraction >= 0.0f) m_value = (int)(std::min(fraction, 1.0f) * 100.0f);
		if (!label.isEmpty()) m_label = label;
		ip->ProgressUpdate(m_value, FALSE, toM(m_label).data());
		return !ip->GetCancel();
	}
	void end() {
		if (m_started) GetCOREInterface()->ProgressEnd();
		m_started = false;
	}

private:
	static DWORD WINAPI noWork(LPVOID) { return 0; }
	bool m_enabled;
	bool m_started = false;
	int m_value = 0;
	QString m_label = "Sisir Import";
};

bool escapePressed() { return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0; }

} // namespace

ImportOutcome importSirFile(const ImportRequest& request) {
	ImportOutcome out;
	SisirDcc::ImportOptions options = request.options;
	const QFileInfo sir(request.file);
	if (!sir.isFile()) {
		out.error = "file not found: " + request.file;
		if (request.interactive) showErrorLater("Sisir Import", out.error);
		else logError(out.error);
		return out;
	}

	// Sisir itself does the generating.
	QString exe = Baker::findSisir(options.sisirExecutable);
	if (exe.isEmpty() && request.interactive) {
		MessageBoxW(GetCOREInterface()->GetMAXHWnd(), L"Sisir was not found on this computer. Show where Sisir.exe is.",
		            L"Sisir Import", MB_OK | MB_ICONINFORMATION);
		exe = QDir::toNativeSeparators(QFileDialog::getOpenFileName(maxWindow(), "Locate Sisir.exe", QString(), "Sisir (Sisir.exe)"));
		if (exe.isEmpty()) {
			out.cancelled = true;
			out.error = "Sisir location needed";
			return out;
		}
	}
	if (exe.isEmpty()) {
		out.error = "Sisir was not found. Install Sisir, run it once, or pass its location with sisir:.";
		logError(out.error);
		return out;
	}
	QString why;
	if (!Baker::canBake(exe, &why)) {
		out.error = why;
		if (request.interactive) showErrorLater("Sisir Import", why);
		else logError(why);
		return out;
	}
	Baker::rememberSisir(exe);

	const QString sceneName = sir.completeBaseName();
	const QString assets = options.textureFolder.isEmpty() ? defaultTextureFolder(sceneName) : options.textureFolder;
	const QString source = sir.absoluteFilePath();

	if (request.showDialog && request.interactive) {
		// Modal, and open at once: Sisir bakes behind it (BakeJob) while its sections wait, and fill
		// when the bake is in. Import builds the scene with the dialog as its progress bar.
		ImportDialog dialog(options, source, maxWindow());
		BakeResult bake;
		bool built = false;
		auto* job = new BakeJob(&dialog); // closing the dialog stops Sisir
		QObject::connect(job, &BakeJob::progress, &dialog, [&dialog](float f, const QString& label) { dialog.setProgress(f, label); });
		QObject::connect(job, &BakeJob::finished, &dialog, [&](const BakeResult& result) {
			bake = result;
			SirBake::Reader reader;
			QString error = result.error;
			if (result.ok && reader.open(result.bakeFile, &error)) {
				dialog.setMeta(reader.meta());
				return;
			}
			out.error = error;
			showErrorLater("Sisir Import", error);
			dialog.reject();
		});
		QObject::connect(&dialog, &ImportDialog::importRequested, &dialog, [&] {
			SisirDcc::ImportOptions chosen = dialog.options();
			chosen.sisirExecutable = request.options.sisirExecutable;
			chosen.remember();
			dialog.setBuilding();
			const Built result = buildScene(bake.bakeFile, bake.assetsDir, chosen, source, true, [&dialog](float f, const QString& label) {
				dialog.showBuildProgress(f, label);
				return !escapePressed();
			});
			built = true;
			out.ok = result.ok;
			out.cancelled = result.cancelled;
			out.error = result.error;
			out.rootName = result.rootName;
			if (result.ok) recordUndo(result, bake.bakeFile, bake.assetsDir, chosen, source);
			else if (!result.cancelled) showErrorLater("Sisir Import", result.error);
			dialog.finishBuilding();
		});
		job->start(source, assets, exe);
		dialog.exec();
		if (!built && out.error.isEmpty()) out.cancelled = true;
		GetCOREInterface()->RedrawViews(GetCOREInterface()->GetTime());
		return out;
	}

	StatusProgress status(request.interactive);
	int lastPercent = -1;
	auto progress = [&](float f, const QString& label) -> bool {
		if (request.interactive) return status.step(f, label);
		const int pct = (int)(f * 100.0f);
		if (f >= 0.0f && pct / 10 != lastPercent / 10) {
			logInfo(QString("%1% %2").arg(pct).arg(label));
			lastPercent = pct;
		}
		return true;
	};
	const float bakeShare = 0.6f;
	progress(0.0f, "Generating hair and cards in Sisir...");
	const BakeResult bake = Baker::bake(source, assets, exe, [&](float f, const QString& label) {
		return progress(f >= 0.0f ? f * bakeShare : f, label.isEmpty() ? QString() : "Sisir: " + label);
	});
	if (!bake.ok) {
		out.cancelled = bake.cancelled;
		out.error = bake.error;
		if (!bake.cancelled) {
			if (request.interactive) showErrorLater("Sisir Import", bake.error);
			else logError(bake.error);
		}
		return out;
	}
	const Built result = buildScene(bake.bakeFile, bake.assetsDir, options, source, request.interactive, [&](float f, const QString& label) {
		return progress(f >= 0.0f ? bakeShare + (1.0f - bakeShare) * f : f, label);
	});
	status.end();
	out.ok = result.ok;
	out.cancelled = result.cancelled;
	out.error = result.error;
	out.rootName = result.rootName;
	if (result.ok) {
		recordUndo(result, bake.bakeFile, bake.assetsDir, options, source);
	} else if (!result.cancelled) {
		if (request.interactive) showErrorLater("Sisir Import", result.error);
		else logError(result.error);
	}
	GetCOREInterface()->RedrawViews(GetCOREInterface()->GetTime());
	return out;
}

} // namespace SisirMax
