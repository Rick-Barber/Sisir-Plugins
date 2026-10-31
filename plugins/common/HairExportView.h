#pragma once

// What a renderer translator reads from Sisir hair, as plain arrays behind a C entry point: Maya's
// sisirHairShape (maya/src/HairExportView.h) and 3ds Max's Sisir Hair object (max/src/HairObject.h).
//
// The translators (SisirMtoA now; V-Ray and Redshift later) are separate DLLs built against their
// renderer's SDK. They never touch the plug-in's C++ classes: they look up sisirGetHairExportView in
// the loaded plug-in and read these arrays, so a translator and the plug-in only have to agree on
// this struct. Bump kSisirHairExportViewVersion on any layout change.
//
// The pointers stay valid until the shape's hair data changes; a translator copies what it needs
// during its export call and keeps nothing.

#include <cstdint>

inline constexpr uint32_t kSisirHairExportViewVersion = 3;

struct SisirHairExportView {
	uint32_t version = kSisirHairExportViewVersion;
	uint32_t strandCount = 0;
	uint32_t pointCount = 0;
	const uint32_t* pointCounts = nullptr; // [strandCount]
	const float* positions = nullptr;      // [pointCount * 3], object space, host units (Maya: cm)
	const float* widths = nullptr;         // [pointCount], full width, host units, widthScale NOT applied
	const uint8_t* albedo = nullptr;       // [pointCount * 4], sRGB
	const float* rootUvs = nullptr;        // [strandCount * 2]
	const int32_t* clumpIds = nullptr;     // [strandCount]
	const float* randoms = nullptr;        // [strandCount]
	// The shape's attributes, as the renderer should apply them.
	float widthScale = 1.0f;
	float renderPercent = 100.0f;
	// Linear Rec.709 (the albedo's space) to the host's rendering space, row-major. Version 2.
	// Version 3: no cameraFacing; hair always renders as camera-facing ribbons, as in Sisir.
	float toRenderingSpace[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
};
