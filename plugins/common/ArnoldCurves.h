#pragma once

// Sisir hair as an Arnold `curves` node, shared by SisirMtoA (Maya) and Sisir for 3ds Max (MAXtoA),
// so both renderers get exactly the same curves:
//
// Catmull-Rom through Sisir's points (the ends extended linearly, as Sisir's renderer does), radius
// per point, ribbons facing the camera (Sisir's look). Per point, Sisir's albedo rides along as
// `sisir_albedo` (in the host's rendering space) for a user-data colour shader; per strand,
// `root_uv`, `clump_id` and `strand_random`. Render % keeps the strands whose random falls under
// it -- the same even subset the viewport's Display % shows.
//
// Header-only and included only where Arnold's headers are: plugins/common links nothing of Arnold.

#include "HairExportView.h"

#include <ai.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace SisirArnold {

inline float srgbToLinear(uint8_t c) {
	const float v = (float)c / 255.0f;
	return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

// An empty curves node: what a translator writes when the hair is missing.
inline void writeEmptyCurves(AtNode* curves) {
	AiNodeSetArray(curves, AtString("num_points"), AiArray(0, 1, AI_TYPE_UINT));
	AiNodeSetArray(curves, AtString("points"), AiArray(0, 1, AI_TYPE_VECTOR));
}

inline void writeCurves(AtNode* curves, const SisirHairExportView& v) {
	if (v.strandCount == 0) {
		writeEmptyCurves(curves);
		return;
	}
	const float keepBelow = std::max(0.0f, std::min(v.renderPercent, 100.0f)) / 100.0f;
	std::vector<uint32_t> kept;
	kept.reserve(v.strandCount);
	std::vector<uint32_t> offsets(v.strandCount);
	uint64_t offset = 0, keptPoints = 0;
	for (uint32_t s = 0; s < v.strandCount; s++) {
		offsets[s] = (uint32_t)offset;
		offset += v.pointCounts[s];
		if (keepBelow >= 1.0f || v.randoms[s] < keepBelow) {
			kept.push_back(s);
			keptPoints += v.pointCounts[s];
		}
	}
	const uint32_t curveCount = (uint32_t)kept.size();
	const uint64_t controlPoints = keptPoints + 2ull * curveCount; // phantom end points

	AtArray* numPoints = AiArrayAllocate(curveCount, 1, AI_TYPE_UINT);
	AtArray* points = AiArrayAllocate((uint32_t)controlPoints, 1, AI_TYPE_VECTOR);
	AtArray* radius = AiArrayAllocate((uint32_t)keptPoints, 1, AI_TYPE_FLOAT);
	AtArray* albedo = AiArrayAllocate((uint32_t)keptPoints, 1, AI_TYPE_RGB);
	AtArray* rootUv = AiArrayAllocate(curveCount, 1, AI_TYPE_VECTOR2);
	AtArray* clump = AiArrayAllocate(curveCount, 1, AI_TYPE_INT);
	AtArray* random = AiArrayAllocate(curveCount, 1, AI_TYPE_FLOAT);
	auto* np = static_cast<uint32_t*>(AiArrayMap(numPoints));
	auto* pp = static_cast<AtVector*>(AiArrayMap(points));
	auto* rp = static_cast<float*>(AiArrayMap(radius));
	auto* ap = static_cast<AtRGB*>(AiArrayMap(albedo));
	auto* uvp = static_cast<AtVector2*>(AiArrayMap(rootUv));
	auto* cp = static_cast<int*>(AiArrayMap(clump));
	auto* sp = static_cast<float*>(AiArrayMap(random));

	float srgb[256];
	for (int i = 0; i < 256; i++) srgb[i] = srgbToLinear((uint8_t)i);
	const float halfWidth = 0.5f * std::max(v.widthScale, 0.0f);
	size_t pw = 0, vw = 0;
	for (uint32_t c = 0; c < curveCount; c++) {
		const uint32_t s = kept[c];
		const uint32_t n = v.pointCounts[s], first = offsets[s];
		np[c] = n + 2;
		auto at = [&](uint32_t i) {
			const float* q = v.positions + (size_t)(first + i) * 3;
			return AtVector(q[0], q[1], q[2]);
		};
		pp[pw++] = at(0) * 2.0f - at(1);
		for (uint32_t i = 0; i < n; i++) pp[pw++] = at(i);
		pp[pw++] = at(n - 1) * 2.0f - at(n - 2);
		for (uint32_t i = 0; i < n; i++, vw++) {
			rp[vw] = v.widths[first + i] * halfWidth;
			const uint8_t* a = v.albedo + (size_t)(first + i) * 4;
			// Linear Rec.709 into the host's rendering space, as texture files are.
			const float lin[3] = { srgb[a[0]], srgb[a[1]], srgb[a[2]] };
			const float* m = v.toRenderingSpace;
			ap[vw] = AtRGB(m[0] * lin[0] + m[1] * lin[1] + m[2] * lin[2],
			               m[3] * lin[0] + m[4] * lin[1] + m[5] * lin[2],
			               m[6] * lin[0] + m[7] * lin[1] + m[8] * lin[2]);
		}
		uvp[c] = AtVector2(v.rootUvs[s * 2], v.rootUvs[s * 2 + 1]);
		cp[c] = v.clumpIds[s];
		sp[c] = v.randoms[s];
	}
	AiArrayUnmap(numPoints);
	AiArrayUnmap(points);
	AiArrayUnmap(radius);
	AiArrayUnmap(albedo);
	AiArrayUnmap(rootUv);
	AiArrayUnmap(clump);
	AiArrayUnmap(random);

	AiNodeSetStr(curves, AtString("basis"), AtString("catmull-rom"));
	AiNodeSetStr(curves, AtString("mode"), AtString("ribbon") /* camera-facing, as Sisir draws hair */);
	AiNodeSetArray(curves, AtString("num_points"), numPoints);
	AiNodeSetArray(curves, AtString("points"), points);
	AiNodeSetArray(curves, AtString("radius"), radius);
	auto declare = [&](const char* name, const char* decl, AtArray* data) {
		if (!AiNodeLookUpUserParameter(curves, AtString(name))) AiNodeDeclare(curves, AtString(name), decl);
		AiNodeSetArray(curves, AtString(name), data);
	};
	declare("sisir_albedo", "varying RGB", albedo); // Sisir's colour, in the host's rendering space
	declare("root_uv", "uniform VECTOR2", rootUv);
	declare("clump_id", "uniform INT", clump);
	declare("strand_random", "uniform FLOAT", random);
}

} // namespace SisirArnold
