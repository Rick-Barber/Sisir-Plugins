#include "RenderingSpace.h"

#include <maya/MGlobal.h>
#include <maya/MString.h>

#include <cstring>

namespace SisirMaya {

namespace {
// Linear Rec.709 (D65) to the ACES primaries (D60), Bradford-adapted: the matrices OCIO's ACES
// configs use.
const float kRec709ToAcescg[9] = {
	0.6130974f, 0.3395231f, 0.0473795f,
	0.0701937f, 0.9163539f, 0.0134524f,
	0.0206156f, 0.1095698f, 0.8698147f,
};
const float kRec709ToAces2065[9] = {
	0.4397010f, 0.3829780f, 0.1773350f,
	0.0897923f, 0.8134230f, 0.0967616f,
	0.0175440f, 0.1115440f, 0.8707040f,
};
const float kIdentity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
} // namespace

void renderingSpaceMatrix(float out[9]) {
	std::memcpy(out, kIdentity, sizeof(kIdentity));
	int enabled = 0;
	MGlobal::executeCommand("colorManagementPrefs -q -cmEnabled", enabled, false, false);
	if (!enabled) return;
	MString space;
	MGlobal::executeCommand("colorManagementPrefs -q -renderingSpaceName", space, false, false);
	const MString lower = MString(space).toLowerCase();
	if (lower.indexW("acescg") >= 0) {
		std::memcpy(out, kRec709ToAcescg, sizeof(kRec709ToAcescg));
	} else if (lower.indexW("aces2065") >= 0) {
		std::memcpy(out, kRec709ToAces2065, sizeof(kRec709ToAces2065));
	}
	// Anything else (scene-linear Rec.709/sRGB, or a custom space): left as is.
}

} // namespace SisirMaya
