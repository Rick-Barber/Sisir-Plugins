#pragma once

namespace SisirMax {

// Linear Rec.709 (Sisir's albedo) to the colour space 3ds Max renders in, row-major. Identity
// unless Max 2024+ runs in an OCIO colour-management mode with another rendering space.
void renderingSpaceMatrix(float m[9]);

} // namespace SisirMax
