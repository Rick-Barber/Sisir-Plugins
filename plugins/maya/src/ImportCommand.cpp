#include "ImportCommand.h"

#include "Baker.h"
#include "ImportDialog.h"
#include "SceneBuilder.h"
#include "SirBakeReader.h"

#include <maya/MArgDatabase.h>
#include <maya/MComputation.h>
#include <maya/MDGModifier.h>
#include <maya/MFnDagNode.h>
#include <maya/MGlobal.h>
#include <maya/MQtUtil.h>

#include <QApplication>
#include <QDir>

#include <algorithm>
#include <QFileInfo>

namespace SisirMaya {

const char* ImportCommand::kName = "sisirImport";

namespace {

MString toM(const QString& s) { return MString(reinterpret_cast<const wchar_t*>(s.utf16())); }
QString toQ(const MString& s) { return QString::fromUtf8(s.asUTF8()); }

QWidget* mayaWindow() { return MQtUtil::mainWindow(); }

// No Qt event loop ever runs inside a Maya command here: no QDialog::exec, no QFileDialog, no
// QMessageBox, no processEvents. Any of them inside a command left Maya's window state corrupted,
// and the next re-dock of a workspace control (Arnold > Render) crashed in QWindowContainer. So
// pickers and messages go through Maya's own MEL UI after the command returns (executeCommandOnIdle),
// and the options dialog is non-modal: its Import button runs the import as a command of its own.

// A string as a MEL string literal.
QString melQuote(const QString& s) {
	QString out = s;
	out.replace("\\", "\\\\").replace("\"", "\\\"");
	return "\"" + out + "\"";
}

QString defaultTextureFolder(const QString& sceneName) {
	MString root, rule;
	MGlobal::executeCommand("workspace -q -rd", root, false, false);
	MGlobal::executeCommand("workspace -fre \"sourceImages\"", rule, false, false);
	QString base = toQ(root);
	if (base.isEmpty()) base = QDir::homePath();
	QString images = toQ(rule);
	if (images.isEmpty()) images = "sourceimages";
	const QString dir = QDir::isAbsolutePath(images) ? images : QDir(base).filePath(images);
	return QDir(dir).filePath("sisir/" + legalMayaName(sceneName));
}

// Progress for an import running as a command. With the import dialog open (it asked for this
// build), its own bar, painted directly; otherwise Maya's progress bar in the main window's status
// line. Never Maya's progress window: it cannot paint while the command holds the main thread, so
// it sat on screen empty. Esc cancels either way.
class ImportProgress {
public:
	explicit ImportProgress(bool enabled) : m_enabled(enabled) {}
	~ImportProgress() { end(); }
	bool step(float fraction, const QString& label) {
		if (!m_enabled) return true;
		if (ImportDialog* dialog = ImportDialog::active(); dialog && dialog->building()) {
			if (!m_computing) {
				m_computation.beginComputation(false, true, true);
				m_computing = true;
			}
			dialog->showBuildProgress(fraction, label);
			return !m_computation.isInterruptRequested();
		}
		const int value = fraction >= 0.0f ? (int)(std::min(fraction, 1.0f) * 1000.0f) : m_value;
		if (!m_statusBar) {
			MGlobal::executeCommand(toM("global string $gMainProgressBar; progressBar -e -beginProgress -isInterruptable true "
			                            "-maxValue 1000 -status " + melQuote(label.isEmpty() ? "Sisir Import" : label) +
			                            " $gMainProgressBar"), false, false);
			m_statusBar = true;
		} else if (value != m_value || (!label.isEmpty() && label != m_label)) {
			QString edit = QString("global string $gMainProgressBar; progressBar -e -progress %1").arg(value);
			if (!label.isEmpty()) edit += " -status " + melQuote(label);
			MGlobal::executeCommand(toM(edit + " $gMainProgressBar"), false, false);
		}
		m_value = value;
		if (!label.isEmpty()) m_label = label;
		int cancelled = 0;
		MGlobal::executeCommand("global string $gMainProgressBar; progressBar -q -isCancelled $gMainProgressBar", cancelled, false, false);
		return cancelled == 0;
	}
	void end() {
		if (m_computing) m_computation.endComputation();
		m_computing = false;
		if (m_statusBar) MGlobal::executeCommand("global string $gMainProgressBar; progressBar -e -endProgress $gMainProgressBar", false, false);
		m_statusBar = false;
	}

private:
	bool m_enabled;
	bool m_computing = false;
	bool m_statusBar = false;
	int m_value = -1;
	QString m_label;
	MComputation m_computation;
};

// Closes the import dialog that asked for a build once that build is over, however it ends.
struct CloseBuildingDialog {
	~CloseBuildingDialog() {
		if (ImportDialog* dialog = ImportDialog::active(); dialog && dialog->building()) dialog->finishBuilding();
	}
};

void report(bool interactive, bool isError, const QString& text) {
	if (isError) MGlobal::displayError(toM("Sisir: " + text));
	else MGlobal::displayInfo(toM("Sisir: " + text));
	if (interactive && isError) {
		MGlobal::executeCommandOnIdle(toM("confirmDialog -title \"Sisir Import\" -button \"OK\" -message " + melQuote(text)), false);
	}
}

} // namespace

ImportOutcome importSirFile(const ImportRequest& request) {
	CloseBuildingDialog closeDialogAtEnd;
	ImportOutcome out;
	ImportOptions options = request.options;
	const QFileInfo sir(request.file);
	if (!sir.isFile()) {
		out.error = "file not found: " + request.file;
		report(request.interactive, true, out.error);
		return out;
	}

	// Sisir itself does the generating.
	QString exe = Baker::findSisir(options.sisirExecutable);
	if (exe.isEmpty() && request.interactive) {
		// Ask where Sisir is once this command has returned, then import again with it.
		MGlobal::executeCommandOnIdle(toM("sisirLocateAndImport " + melQuote(QDir::fromNativeSeparators(sir.absoluteFilePath()))), false);
		out.cancelled = true;
		out.error = "Sisir location needed";
		return out;
	}
	if (exe.isEmpty()) {
		out.error = "Sisir was not found. Install Sisir, run it once, or pass its location with -sisir.";
		report(request.interactive, true, out.error);
		return out;
	}
	QString why;
	if (!Baker::canBake(exe, &why)) {
		out.error = why;
		report(request.interactive, true, out.error);
		return out;
	}
	Baker::rememberSisir(exe);

	const QString sceneName = sir.completeBaseName();
	const QString assets = options.textureFolder.isEmpty() ? defaultTextureFolder(sceneName) : options.textureFolder;

	if (request.showDialog && request.interactive) {
		// Non-modal, and open at once: Sisir bakes behind it (BakeJob) while Maya stays responsive,
		// and its sections fill when the bake is in. Import then runs `sisirImport -noDialog` with
		// the choices, which finds this bake in the cache and reports its build to this dialog.
		auto* dialog = new ImportDialog(options, sir.absoluteFilePath(), mayaWindow());
		dialog->setAttribute(Qt::WA_DeleteOnClose);
		auto* job = new BakeJob(dialog); // closing the dialog stops Sisir
		QObject::connect(job, &BakeJob::progress, dialog, [dialog](float f, const QString& label) {
			dialog->setProgress(f, label);
		});
		QObject::connect(job, &BakeJob::finished, dialog, [dialog](const BakeResult& bake) {
			SirBake::Reader reader;
			QString error = bake.error;
			if (bake.ok && reader.open(bake.bakeFile, &error)) {
				dialog->setMeta(reader.meta());
				return;
			}
			report(true, true, error);
			dialog->close();
		});
		const QString file = QDir::fromNativeSeparators(sir.absoluteFilePath());
		const QString sisirOverride = request.options.sisirExecutable;
		QObject::connect(dialog, &ImportDialog::importRequested, dialog, [dialog, file, sisirOverride] {
			ImportOptions chosen = dialog->options();
			chosen.sisirExecutable = sisirOverride;
			chosen.remember();
			dialog->setBuilding();
			MGlobal::executeCommandOnIdle(toM("sisirImport -noDialog -file " + melQuote(file) + " -options " + melQuote(chosen.toString())), true);
		});
		dialog->show();
		dialog->raise();
		job->start(sir.absoluteFilePath(), assets, exe);
		out.dialogShown = true;
		return out;
	}

	ImportProgress progressUi(request.interactive);
	int lastPercent = -1;
	auto progress = [&](float f, const QString& label) -> bool {
		if (request.interactive) return progressUi.step(f, label);
		const int pct = (int)(f * 100.0f);
		if (f >= 0.0f && pct / 10 != lastPercent / 10) {
			MGlobal::displayInfo(toM(QString("Sisir: %1% %2").arg(pct).arg(label)));
			lastPercent = pct;
		}
		return true;
	};

	// Under the import dialog the bake is normally in the cache already, so the build has the bar.
	const ImportDialog* dialog = ImportDialog::active();
	const float bakeShare = request.interactive && dialog && dialog->building() ? 0.0f : 0.6f;
	if (bakeShare > 0.0f) progressUi.step(0.0f, "Generating hair and cards in Sisir...");
	const BakeResult bake = Baker::bake(sir.absoluteFilePath(), assets, exe, [&](float f, const QString& label) {
		return progress(f >= 0.0f ? f * bakeShare : f, label.isEmpty() ? QString() : "Sisir: " + label);
	});
	if (!bake.ok) {
		out.cancelled = bake.cancelled;
		out.error = bake.error;
		if (!bake.cancelled) report(request.interactive, true, bake.error);
		return out;
	}

	SirBake::Reader reader;
	QString error;
	if (!reader.open(bake.bakeFile, &error)) {
		out.error = error;
		report(request.interactive, true, error);
		return out;
	}

	SceneBuilder builder(reader, options, bake.assetsDir, QDir::toNativeSeparators(sir.absoluteFilePath()));
	BuildReport built;
	const bool ok = builder.build(built, &error, [&](float f, const QString& label) {
		return progress(f >= 0.0f ? bakeShare + (1.0f - bakeShare) * f : f, label);
	});
	out.created = builder.createdNodes();
	progressUi.end();
	if (!ok) {
		deleteCreated(out.created);
		out.created.clear();
		out.cancelled = error == "Import cancelled";
		out.error = error;
		if (!out.cancelled) report(request.interactive, true, error);
		return out;
	}
	for (const QString& w : built.warnings) MGlobal::displayWarning(toM("Sisir: " + w));
	report(false, false, QString("imported %1: %2 hair sets (%3 strands), %4 guide curves, %5 card layers, %6 meshes")
	                         .arg(sceneName).arg(built.hairSets).arg(QLocale().toString(built.hairStrands))
	                         .arg(built.guideCurves).arg(built.cardLayers).arg(built.meshes));
	out.ok = true;
	out.options = options;
	out.rootName = built.root.isNull() ? QString() : toQ(MFnDagNode(built.root).fullPathName());
	return out;
}

void deleteCreated(const std::vector<MObjectHandle>& created) {
	// DAG nodes go with their topmost created ancestor; everything else (shaders, file nodes,
	// shading groups) by name. Handles that died along the way are skipped. Deleted through Maya's
	// own `delete`, not a short-lived MDGModifier: a modifier destroyed after deleting nodes inside
	// undoIt leaves Maya holding stale references, and the next File > New crashes.
	MString names;
	for (auto it = created.rbegin(); it != created.rend(); ++it) {
		if (!it->isValid() || !it->isAlive()) continue;
		MObject node = it->object();
		MString name;
		if (node.hasFn(MFn::kDagNode)) {
			MFnDagNode dag(node);
			if (dag.parentCount() == 0 || !dag.parent(0).hasFn(MFn::kWorld)) continue;
			name = dag.fullPathName();
		} else {
			name = MFnDependencyNode(node).name();
		}
		if (name.length()) names += " \"" + name + "\"";
	}
	if (names.length()) MGlobal::executeCommand("catchQuiet(`delete" + names + "`)", false, false);
}

MSyntax ImportCommand::newSyntax() {
	MSyntax syntax;
	syntax.addFlag("-f", "-file", MSyntax::kString);
	syntax.addFlag("-o", "-options", MSyntax::kString);
	syntax.addFlag("-nd", "-noDialog");
	syntax.addFlag("-s", "-sisir", MSyntax::kString);
	syntax.enableQuery(false);
	syntax.enableEdit(false);
	return syntax;
}

MStatus ImportCommand::doIt(const MArgList& args) {
	MStatus status;
	MArgDatabase db(syntax(), args, &status);
	if (!status) return status;
	m_request = ImportRequest();
	m_request.options = ImportOptions::loadRemembered();
	MString value;
	if (db.isFlagSet("-options") && db.getFlagArgument("-options", 0, value)) {
		m_request.options = ImportOptions::fromString(toQ(value), m_request.options);
	}
	if (db.isFlagSet("-sisir") && db.getFlagArgument("-sisir", 0, value)) m_request.options.sisirExecutable = toQ(value);
	const bool interactive = MGlobal::mayaState() == MGlobal::kInteractive;
	m_request.interactive = interactive;
	m_request.showDialog = interactive && !db.isFlagSet("-noDialog");
	if (db.isFlagSet("-file") && db.getFlagArgument("-file", 0, value)) {
		m_request.file = toQ(value);
	} else if (interactive) {
		// The file picker runs after this command, then calls sisirImport again with the file.
		MGlobal::executeCommandOnIdle("sisirImportBrowse", false);
		return MS::kSuccess;
	} else {
		displayError("sisirImport: -file is required");
		return MS::kInvalidParameter;
	}

	ImportOutcome outcome = importSirFile(m_request);
	if (!outcome.ok) {
		if (outcome.cancelled || outcome.dialogShown) return MS::kSuccess;
		displayError(toM(outcome.error));
		return MS::kFailure;
	}
	m_created = std::move(outcome.created);
	m_undoable = true;
	// A redo replays the choices just made, without asking again.
	m_request.options = outcome.options;
	m_request.showDialog = false;
	setResult(toM(outcome.rootName));
	return MS::kSuccess;
}

MStatus ImportCommand::undoIt() {
	deleteCreated(m_created);
	m_created.clear();
	return MS::kSuccess;
}

MStatus ImportCommand::redoIt() {
	ImportRequest replay = m_request;
	replay.interactive = false;
	ImportOutcome outcome = importSirFile(replay);
	if (!outcome.ok) return MS::kFailure;
	m_created = std::move(outcome.created);
	setResult(toM(outcome.rootName));
	return MS::kSuccess;
}

} // namespace SisirMaya
