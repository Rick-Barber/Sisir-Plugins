#pragma once

// Viewport colour helpers shared by the Maya and 3ds Max hair drawing, so a material shows the
// same colour in both viewports.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace SisirDcc {

inline float srgbToLinear(float v) {
	v = std::clamp(v, 0.0f, 1.0f);
	return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

inline float srgbToLinear(uint8_t c) { return srgbToLinear((float)c / 255.0f); }

// A card material's Tint is a colour picker value, stored sRGB: Sisir's card shader multiplies the
// texture by it linearised (cards_standalone.hlsl, srgb_to_linear(baseColorTint)), so the plug-ins
// hand renderers the same linear value -- the raw numbers render far too light and grey.
inline void cardTintLinear(const float srgb[3], float out[3]) {
	for (int k = 0; k < 3; k++) out[k] = srgbToLinear(srgb[k]);
}

// What Arnold renders for aiStandardHair at melanin 0, 0.1 .. 1 (columns) and melanin redness
// 0, 0.5, 1 (rows): a patch of hair under a white sky dome, median pixel, sRGB. Measured with
// tools/arnold_melanin_calibration.py (MtoA 5.4); interpolated, so the viewport shows the colour
// the render will have rather than a model's guess at it.
inline constexpr unsigned char kArnoldMelanin[3][11][3] = {
	{ { 255, 255, 255 }, { 230, 225, 205 }, { 177, 163, 119 }, { 122, 104, 59 }, { 79, 61, 29 }, { 50, 35, 16 }, { 31, 20, 11 }, { 20, 13, 9 }, { 14, 10, 8 }, { 11, 9, 8 }, { 9, 8, 8 } },
	{ { 255, 255, 255 }, { 232, 227, 202 }, { 183, 167, 114 }, { 131, 109, 55 }, { 89, 66, 27 }, { 59, 39, 15 }, { 38, 23, 11 }, { 25, 15, 9 }, { 18, 11, 9 }, { 13, 9, 8 }, { 11, 9, 8 } },
	{ { 255, 255, 255 }, { 235, 229, 199 }, { 189, 172, 110 }, { 140, 115, 52 }, { 99, 72, 26 }, { 69, 43, 15 }, { 47, 26, 11 }, { 32, 17, 10 }, { 23, 12, 9 }, { 17, 10, 8 }, { 13, 9, 8 } },
};

// Linear colour for a melanin / redness pair, bilinear over the measured table.
inline void arnoldMelaninColor(float melanin, float redness, float out[3]) {
	const float m = std::clamp(melanin, 0.0f, 1.0f) * 10.0f;
	const float r = std::clamp(redness, 0.0f, 1.0f) * 2.0f;
	const int m0 = std::min((int)m, 9), r0 = std::min((int)r, 1);
	const float fm = m - (float)m0, fr = r - (float)r0;
	for (int k = 0; k < 3; k++) {
		auto at = [&](int ri, int mi) { return srgbToLinear(kArnoldMelanin[ri][mi][k]); };
		const float lo = at(r0, m0) + (at(r0, m0 + 1) - at(r0, m0)) * fm;
		const float hi = at(r0 + 1, m0) + (at(r0 + 1, m0 + 1) - at(r0 + 1, m0)) * fm;
		out[k] = lo + (hi - lo) * fr;
	}
}

// Samples a texture-driven base colour takes along the strand, root (0) to tip (1).
inline constexpr int kGradientSamples = 16;

} // namespace SisirDcc
