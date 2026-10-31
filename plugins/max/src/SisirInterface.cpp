// `sisir`: what the Sisir menu (scripts/SisirMenu.ms) and any MAXScript or Python script calls --
// the counterpart of Sisir for Maya's sisirImport and sisirExportAbc commands, with the same
// option strings:
//
//   sisir.importFile "C:/heads/anna.sir"                                  -- the dialog, as File > Import
//   sisir.importFile "anna.sir" options:"cards=0;guideDegree=3" noDialog:true
//   sisir.importFile "anna.sir" noDialog:true sisir:"D:/Sisir/Sisir.exe"
//   sisir.importBrowse()                                                  -- pick a file, then the dialog
//   sisir.exportAbc "C:/out/anna.abc" options:"units=cm;unreal=1" noDialog:true
//   sisir.exportAbcCount()                                                -- hair the selection would export
//   sisir.exportAbcBrowse()                                               -- the menu's flow
//   sisir.exportGuidesPly()                                               -- Scripts/3DSMax/3DSMax_to_PLY.py
//   sisir.plyUnit()                                                       -- Export Guides (PLY)'s unit popup
//   sisir.pluginPath()                                                    -- which SisirForMax.dlo is loaded
//   sisir.hairStats $Bangs                                                -- strands, points, content hash
//
// importFile returns the root node's name, empty when nothing was imported (or the dialog was
// cancelled); exportAbc the written file, empty on failure.

#include "ClassIds.h"
#include "ExportAbc.h"
#include "HairObject.h"
#include "ImportFlow.h"
#include "MaxUtil.h"
#include "PlyExportDialog.h"

#include <ifnpub.h>
#include <inode.h>
#include <Qt/QmaxMainWindow.h>

#include <QDir>
#include <QFileDialog>

namespace SisirMax {

namespace {

class SisirInterface : public FPStaticInterface {
public:
	DECLARE_DESCRIPTOR(SisirInterface)

	enum FunctionId { fnImportFile, fnImportBrowse, fnExportAbc, fnExportAbcCount, fnExportAbcBrowse, fnExportGuidesPly, fnPluginPath, fnHairStats, fnPlyUnit };

	BEGIN_FUNCTION_MAP
		FN_4(fnImportFile, TYPE_TSTR_BV, importFile, TYPE_STRING, TYPE_STRING, TYPE_BOOL, TYPE_STRING)
		FN_0(fnImportBrowse, TYPE_TSTR_BV, importBrowse)
		FN_3(fnExportAbc, TYPE_TSTR_BV, exportAbcFile, TYPE_STRING, TYPE_STRING, TYPE_BOOL)
		FN_0(fnExportAbcCount, TYPE_INT, exportAbcCount)
		VFN_0(fnExportAbcBrowse, exportAbcBrowse)
		VFN_0(fnExportGuidesPly, exportGuidesPly)
		FN_0(fnPluginPath, TYPE_TSTR_BV, pluginPathString)
		FN_1(fnHairStats, TYPE_TSTR_BV, hairStats, TYPE_INODE)
		FN_0(fnPlyUnit, TYPE_TSTR_BV, plyUnit)
	END_FUNCTION_MAP

	MSTR importFile(const MCHAR* file, const MCHAR* options, BOOL noDialog, const MCHAR* sisir) {
		ImportRequest request;
		request.file = toQ(file);
		request.options = SisirDcc::ImportOptions::loadRemembered();
		if (options && *options) request.options = SisirDcc::ImportOptions::fromString(toQ(options), request.options);
		if (sisir && *sisir) request.options.sisirExecutable = toQ(sisir);
		request.interactive = interactive();
		request.showDialog = request.interactive && !noDialog;
		const ImportOutcome outcome = importSirFile(request);
		return outcome.ok ? toM(outcome.rootName) : MSTR();
	}

	MSTR importBrowse() {
		const QString file = QFileDialog::getOpenFileName(GetCOREInterface()->GetQmaxMainWindow(), "Import Sisir Scene", QString(),
		                                                  "Sisir Scenes (*.sir)");
		if (file.isEmpty()) return MSTR();
		return importFile(toM(file).data(), _M(""), FALSE, _M(""));
	}

	MSTR exportAbcFile(const MCHAR* file, const MCHAR* options, BOOL noDialog) {
		return toM(exportAbc(toQ(file), toQ(options), !noDialog));
	}

	int exportAbcCount() { return exportAbcTargetCount(); }

	void exportAbcBrowse() { exportAbcInteractive(); }

	void exportGuidesPly() {
		const QString script = QDir(contentsDir()).filePath("scripts/3DSMax_to_PLY.py");
		// The exporter raises on an empty selection: shown as a message, not a Python traceback.
		const QString python = QString("import runpy, pymxs\n"
		                               "try:\n"
		                               "    runpy.run_path(r'%1', run_name='sisir_guides_ply')['export_sisir_guides']()\n"
		                               "except RuntimeError as e:\n"
		                               "    pymxs.runtime.messageBox(str(e), title='Export Guides (PLY)')\n")
		                           .arg(QDir::toNativeSeparators(script));
		runMaxScript("python.Execute " + msQuote(python));
	}

	MSTR pluginPathString() { return toM(QDir::toNativeSeparators(pluginPath())); }

	// The unit Export Guides (PLY) writes in, asked in Sisir's look and remembered: "mm", "cm" or
	// "m", empty when cancelled. 3DSMax_to_PLY.py asks through this.
	MSTR plyUnit() { return toM(SisirDcc::askPlyUnit(GetCOREInterface()->GetQmaxMainWindow())); }

	// "strands=<n>;points=<n>;hash=<hex>" for a Sisir Hair node, empty for anything else: what the
	// tests compare across save and reload.
	MSTR hairStats(INode* node) {
		Object* obj = node ? node->EvalWorldState(GetCOREInterface()->GetTime()).obj : nullptr;
		if (!obj || obj->ClassID() != kHairObjectClassId) return MSTR();
		auto b = static_cast<HairObject*>(obj)->buffers();
		if (!b) return toM("strands=0;points=0;hash=0");
		uint64_t h = 1469598103934665603ull;
		auto mix = [&](const void* data, size_t bytes) {
			const auto* p = static_cast<const unsigned char*>(data);
			for (size_t i = 0; i < bytes; i++) h = (h ^ p[i]) * 1099511628211ull;
		};
		mix(b->pointCounts.data(), b->pointCounts.size() * 4);
		mix(b->positions.data(), b->positions.size() * 4);
		mix(b->widths.data(), b->widths.size() * 4);
		mix(b->albedo.data(), b->albedo.size());
		mix(b->rootUvs.data(), b->rootUvs.size() * 4);
		mix(b->clumpIds.data(), b->clumpIds.size() * 4);
		mix(b->randoms.data(), b->randoms.size() * 4);
		return toM(QString("strands=%1;points=%2;hash=%3").arg(b->strandCount()).arg(b->pointCount()).arg(h, 16, 16, QChar('0')));
	}
};

SisirInterface g_interface(kSisirInterfaceId, _M("sisir"), 0, nullptr, FP_CORE,
	SisirInterface::fnImportFile, _M("importFile"), 0, TYPE_TSTR_BV, 0, 4,
		_M("file"), 0, TYPE_STRING,
		_M("options"), 0, TYPE_STRING, f_keyArgDefault, _M(""),
		_M("noDialog"), 0, TYPE_BOOL, f_keyArgDefault, FALSE,
		_M("sisir"), 0, TYPE_STRING, f_keyArgDefault, _M(""),
	SisirInterface::fnImportBrowse, _M("importBrowse"), 0, TYPE_TSTR_BV, 0, 0,
	SisirInterface::fnExportAbc, _M("exportAbc"), 0, TYPE_TSTR_BV, 0, 3,
		_M("file"), 0, TYPE_STRING,
		_M("options"), 0, TYPE_STRING, f_keyArgDefault, _M(""),
		_M("noDialog"), 0, TYPE_BOOL, f_keyArgDefault, FALSE,
	SisirInterface::fnExportAbcCount, _M("exportAbcCount"), 0, TYPE_INT, 0, 0,
	SisirInterface::fnExportAbcBrowse, _M("exportAbcBrowse"), 0, TYPE_VOID, 0, 0,
	SisirInterface::fnExportGuidesPly, _M("exportGuidesPly"), 0, TYPE_VOID, 0, 0,
	SisirInterface::fnPluginPath, _M("pluginPath"), 0, TYPE_TSTR_BV, 0, 0,
	SisirInterface::fnHairStats, _M("hairStats"), 0, TYPE_TSTR_BV, 0, 1,
		_M("node"), 0, TYPE_INODE,
	SisirInterface::fnPlyUnit, _M("plyUnit"), 0, TYPE_TSTR_BV, 0, 0,
	p_end);

} // namespace

} // namespace SisirMax
