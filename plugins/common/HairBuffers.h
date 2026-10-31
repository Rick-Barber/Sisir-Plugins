#pragma once

// The strands of one hair set, as the plug-ins hold them: what Maya's sisirHairShape and 3ds Max's
// Sisir Hair object store, draw, render and save.
//
// HairBuffers are immutable once finalized and shared: the hosts copy their data objects freely,
// and with shared buffers those copies cost a pointer, so a strand array of a few hundred MB is
// never duplicated. Edits make a new buffer, never change one in place.

#include <cstdint>
#include <memory>
#include <vector>

namespace SisirDcc {

struct HairBuffers {
	std::vector<uint32_t> pointCounts; // per strand, >= 2
	std::vector<uint32_t> offsets;     // per strand + 1: first point of each strand
	std::vector<float> positions;      // xyz per point, object space, host units
	std::vector<float> widths;         // per point, full width, host units
	std::vector<uint8_t> albedo;       // rgba per point, sRGB
	std::vector<float> rootUvs;        // uv per strand
	std::vector<int32_t> clumpIds;     // per strand
	std::vector<float> randoms;        // per strand, [0, 1)
	float boundsMin[3] = { 0, 0, 0 };
	float boundsMax[3] = { 0, 0, 0 };
	float maxWidth = 0.0f;
	uint64_t revision = 0;             // process-unique, for viewport caches

	uint32_t strandCount() const { return (uint32_t)pointCounts.size(); }
	uint32_t pointCount() const { return (uint32_t)widths.size(); }
	// Offsets, bounds, maxWidth and a fresh revision, from the arrays. False when they disagree.
	bool finalize();
};

// The serialized form every scene file stores: a zlib-compressed block (layout in the .cpp).
std::vector<char> serializeHair(const HairBuffers& b);
std::shared_ptr<HairBuffers> deserializeHair(const char* data, size_t size);

} // namespace SisirDcc
