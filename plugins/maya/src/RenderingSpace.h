#pragma once

// Sisir's colours are linear Rec.709 (sRGB primaries). Maya renders in its colour management's
// rendering space -- ACEScg by default since 2022 -- and texture files are converted into it, but
// raw colours (per-point user data, the viewport's vertex colours) are not. Without converting them,
// Rec.709 numbers read as ACEScg and saturated hair (an orange cat) shifts towards gold.

namespace SisirMaya {

// Row-major 3x3 taking linear Rec.709 to the current rendering space; identity when colour
// management is off or the rendering space is linear Rec.709 already. Main thread only (asks Maya).
void renderingSpaceMatrix(float out[9]);

inline void applyMatrix(const float m[9], const float in[3], float out[3]) {
	for (int r = 0; r < 3; r++) out[r] = m[r * 3] * in[0] + m[r * 3 + 1] * in[1] + m[r * 3 + 2] * in[2];
}

} // namespace SisirMaya
