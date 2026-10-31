// Sisir for 3ds Max: the DLL entry points 3ds Max calls, and the classes the plug-in registers.
//
//   Sisir Hair       the hair object (HairObject.cpp)
//   Sisir Scene      the .sir importer: File > Import, drag and drop (SirImporter.cpp)
//   sisir            the MAXScript / Python interface the Sisir menu calls (SisirInterface.cpp)

#include "HairObject.h"
#include "HairRenderItem.h"
#include "HostInfo.h"
#include "MaxUtil.h"

#include <iparamb2.h>
#include <plugapi.h>

#include <QDir>

namespace SisirMax {
ClassDesc2* importerClassDesc();
}

using namespace SisirMax;

BOOL WINAPI DllMain(HINSTANCE instance, ULONG reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		setHInstance(instance);
		DisableThreadLibraryCalls(instance);
	}
	return TRUE;
}

extern "C" {

__declspec(dllexport) const MCHAR* LibDescription() { return _M("Sisir for 3ds Max: import Sisir scenes, hair rendered in Arnold"); }

__declspec(dllexport) int LibNumberClasses() { return 2; }

__declspec(dllexport) ClassDesc* LibClassDesc(int i) {
	switch (i) {
	case 0: return HairObject::classDesc();
	case 1: return importerClassDesc();
	default: return nullptr;
	}
}

__declspec(dllexport) ULONG LibVersion() { return VERSION_3DSMAX; }

__declspec(dllexport) int LibInitialize() {
	SisirDcc::setHost(SisirDcc::HostInfo{ "SisirForMax", "3ds Max", "max", "Project sceneassets/images" });
	HairRenderItem::setShaderDirectory(QDir(contentsDir()).filePath("shaders"));
	return TRUE;
}

__declspec(dllexport) int LibShutdown() { return TRUE; }

} // extern "C"
