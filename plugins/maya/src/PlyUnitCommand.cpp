#include "PlyUnitCommand.h"

#include "DccShared.h"
#include "ImportDialog.h"
#include "PlyExportDialog.h"

#include <maya/MArgDatabase.h>
#include <maya/MGlobal.h>
#include <maya/MQtUtil.h>

#include <QApplication>
#include <QDir>
#include <QTimer>
#include <QWidget>

namespace SisirMaya {

namespace {

MString toM(const QString& s) { return MString(reinterpret_cast<const wchar_t*>(s.utf16())); }
QString toQ(const MString& s) { return QString::fromUtf8(s.asUTF8()); }

// A Python string literal for any path.
QString pyQuote(QString s) {
	return "'" + s.replace("\\", "\\\\").replace("'", "\\'") + "'";
}

} // namespace

const char* PlyUnitCommand::kName = "sisirPlyUnit";

MSyntax PlyUnitCommand::newSyntax() {
	MSyntax syntax;
	syntax.addFlag("-f", "-file", MSyntax::kString);
	syntax.enableQuery(false);
	syntax.enableEdit(false);
	return syntax;
}

MStatus PlyUnitCommand::doIt(const MArgList& args) {
	MStatus status;
	MArgDatabase db(syntax(), args, &status);
	if (!status) return status;
	MString value;
	if (!db.isFlagSet("-file") || !db.getFlagArgument("-file", 0, value) || value.length() == 0 ||
	    MGlobal::mayaState() != MGlobal::kInteractive) {
		setResult(toM(rememberedPlyUnit()));
		return MS::kSuccess;
	}
	// The dialog runs a modal loop, so not inside this command: from Maya's event loop once it has
	// returned, then the export runs with the chosen unit.
	const QString file = QDir::fromNativeSeparators(toQ(value));
	QTimer::singleShot(0, qApp, [file]() {
		QWidget host(MQtUtil::mainWindow());
		host.setAttribute(Qt::WA_WindowPropagation);
		host.hide();
		applySisirLook(&host);
		const QString unit = askPlyUnit(&host);
		if (unit.isEmpty()) return;
		MGlobal::executePythonCommand(toM(QString("import Maya_curves_to_PLY; Maya_curves_to_PLY.export_selected_curves_to_ply(path=%1, unit=%2)")
		                                      .arg(pyQuote(file), pyQuote(unit))));
	});
	setResult(MString());
	return MS::kSuccess;
}

} // namespace SisirMaya
