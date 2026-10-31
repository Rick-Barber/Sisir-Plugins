// SisirMtoA: renders sisirHairShape in Arnold.
//
// Each shape becomes one Arnold `curves` node, written by common/ArnoldCurves.h -- the same code
// Sisir for 3ds Max renders with: camera-facing Catmull-Rom ribbons, radius per point, Sisir's albedo
// as `sisir_albedo` (in Maya's rendering space) for aiUserDataColor, and per-strand user data.
//
// The strands come from SisirForMaya.mll through its C entry point (HairExportView.h), so this DLL
// shares no C++ classes with the plug-in.

#include "HairExportView.h"
#include "../../common/ArnoldCurves.h"

#include "extension/Extension.h"
#include "translators/shape/ShapeTranslator.h"

#include <ai.h>

#include <maya/MDagPath.h>
#include <maya/MPlug.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

SisirGetHairExportViewFn exportViewFunction() {
	static SisirGetHairExportViewFn fn = [] {
		HMODULE plugin = GetModuleHandleW(L"SisirForMaya.mll");
		return plugin ? reinterpret_cast<SisirGetHairExportViewFn>(GetProcAddress(plugin, kSisirGetHairExportViewSymbol)) : nullptr;
	}();
	return fn;
}

class SisirHairTranslator : public CShapeTranslator {
public:
	static void* creator() { return new SisirHairTranslator(); }

	static void nodeInitializer(CAbTranslator context) {
		CExtensionAttrHelper helper(context.maya, "curves");
		MakeCommonAttributes(helper);
	}

	AtNode* CreateArnoldNodes() override { return AddArnoldNode("curves"); }

	void Export(AtNode* curves) override {
		ExportMatrix(curves);
		ProcessRenderFlags(curves);
		exportCurves(curves);
		if (RequiresShaderExport()) {
			const MDagPath& path = GetMayaDagPath();
			MPlug shadingGroup = GetNodeShadingGroup(path.node(), path.instanceNumber());
			if (!shadingGroup.isNull()) {
				if (AtNode* shader = ExportConnectedNode(shadingGroup)) AiNodeSetPtr(curves, AtString("shader"), shader);
			}
		}
	}

	void ExportMotion(AtNode* curves) override { ExportMatrix(curves); }

private:
	void exportCurves(AtNode* curves) {
		SisirGetHairExportViewFn getView = exportViewFunction();
		SisirHairExportView v;
		const MObject node = GetMayaObject();
		if (!getView || !getView(&node, &v) || v.version != kSisirHairExportViewVersion) {
			SisirArnold::writeEmptyCurves(curves);
			return;
		}
		SisirArnold::writeCurves(curves, v);
	}
};

} // namespace

extern "C" {

DLLEXPORT void initializeExtension(CExtension& extension) {
	extension.Requires("SisirForMaya");
	extension.RegisterTranslator("sisirHairShape", "sisirHair", SisirHairTranslator::creator, SisirHairTranslator::nodeInitializer);
}

DLLEXPORT void deinitializeExtension(CExtension&) {}

EXPORT_API_VERSION

} // extern "C"
