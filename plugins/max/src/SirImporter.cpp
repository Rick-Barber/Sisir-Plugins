// "Sisir Scene" in File > Import (and a .sir dropped on a viewport): the import dialog, or with
// prompts suppressed (importFile "x.sir" #noPrompt) the remembered options -- as Sisir for Maya's
// file translator does with File > Import.

#include "ClassIds.h"
#include "ImportFlow.h"
#include "MaxUtil.h"

#include <impexp.h>
#include <iparamb2.h>

namespace SisirMax {

namespace {

class SirImporter : public SceneImport {
public:
	int ExtCount() override { return 1; }
	const MCHAR* Ext(int) override { return _M("sir"); }
	const MCHAR* LongDesc() override { return _M("Sisir Scene"); }
	const MCHAR* ShortDesc() override { return _M("Sisir Scene"); }
	const MCHAR* AuthorName() override { return _M("Sisir"); }
	const MCHAR* CopyrightMessage() override { return _M(""); }
	const MCHAR* OtherMessage1() override { return _M(""); }
	const MCHAR* OtherMessage2() override { return _M(""); }
	unsigned int Version() override { return 130; }
	void ShowAbout(HWND) override {}
	int ZoomExtents() override { return ZOOMEXT_NO; }

	int DoImport(const MCHAR* name, ImpInterface*, Interface*, BOOL suppressPrompts) override {
		ImportRequest request;
		request.file = toQ(name);
		request.options = SisirDcc::ImportOptions::loadRemembered();
		request.interactive = interactive();
		request.showDialog = request.interactive && !suppressPrompts;
		const ImportOutcome outcome = importSirFile(request);
		if (outcome.ok) return IMPEXP_SUCCESS;
		return outcome.cancelled ? IMPEXP_CANCEL : IMPEXP_FAIL;
	}
};

class SirImporterClassDesc : public ClassDesc2 {
public:
	int IsPublic() override { return TRUE; }
	void* Create(BOOL) override { return new SirImporter(); }
	const MCHAR* ClassName() override { return _M("Sisir Scene Import"); }
	const MCHAR* NonLocalizedClassName() override { return _M("Sisir Scene Import"); }
	SClass_ID SuperClassID() override { return SCENE_IMPORT_CLASS_ID; }
	Class_ID ClassID() override { return kImporterClassId; }
	const MCHAR* Category() override { return _M("Sisir"); }
	const MCHAR* InternalName() override { return _M("SisirSceneImport"); }
	HINSTANCE HInstance() override { return SisirMax::hInstance(); }
};

SirImporterClassDesc g_importerDesc;

} // namespace

ClassDesc2* importerClassDesc() { return &g_importerDesc; }

} // namespace SisirMax
