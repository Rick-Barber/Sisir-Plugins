#include "RenderingSpace.h"

#if SISIR_MAX_VERSION >= 2024
#include <ColorManagement/ColorConverter.h>
#include <ColorManagement/IColorPipeline.h>
#include <ColorManagement/IColorPipelineMgr.h>
#include <ColorManagement/IModeSettings.h>
#include <max.h>
#endif

namespace SisirMax {

void renderingSpaceMatrix(float m[9]) {
	for (int i = 0; i < 9; i++) m[i] = (i % 4 == 0) ? 1.0f : 0.0f;
#if SISIR_MAX_VERSION >= 2024
	// 3ds Max 2024+ in an OCIO mode renders in its rendering colour space (ACEScg by default);
	// Sisir's albedo is linear Rec.709. The gamma workflow (and Max 2022/2023) renders linear sRGB.
	using namespace MaxSDK::ColorManagement;
	IColorPipelineMgr* mgr = IColorPipelineMgr::GetInstance();
	const IModeSettings* settings = mgr ? mgr->Settings() : nullptr;
	if (!settings || !settings->IsOCIOBased()) return;
	const MSTR target = settings->GetCurrentRenderingColorSpace();
	for (const MCHAR* source : { _M("Linear Rec.709 (sRGB)"), _M("lin_rec709"), _M("scene-linear Rec.709-sRGB") }) {
		if (MSTR(source) == target) return;
		std::unique_ptr<IColorPipeline> pipeline = mgr->GetPipeline(source, target);
		if (!pipeline) continue;
		auto converter = pipeline->GetColorConverter<Color, Color>();
		if (!converter) continue;
		const Color basis[3] = { Color(1, 0, 0), Color(0, 1, 0), Color(0, 0, 1) };
		Color out[3];
		converter->ConvertLine(basis, out, 3);
		// Column c of the matrix is where unit primary c lands.
		for (int c = 0; c < 3; c++) {
			m[0 * 3 + c] = out[c].r;
			m[1 * 3 + c] = out[c].g;
			m[2 * 3 + c] = out[c].b;
		}
		return;
	}
#endif
}

} // namespace SisirMax
