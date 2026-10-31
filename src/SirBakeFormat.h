#pragma once

// The .sirbake file: everything a DCC importer needs from a .sir scene, already generated.
//
// A .sir holds guides, card spines and parameters; the hair and the card meshes are rebuilt from
// them by Sisir's own generators, which no outside reader can reproduce exactly. `Sisir.exe --bake`
// runs those generators headless and writes their output here, so the Maya / 3ds Max / Blender
// importers are pure readers. This header is the whole contract: it has no dependencies so the
// plugins compile it as-is, and every reader validates sizes and CRCs instead of trusting them.
//
// Layout (little-endian):
//   FileHeader
//   { ChunkHeader, payload, zero padding to 8 bytes } * chunkCount
//
// Coordinates are Sisir world space: metres, Y up, right-handed. UVs are bottom-left origin (the
// DCC convention; already flipped from Sisir's image-space card atlas). Asset paths in META are
// relative to the assets directory given on the command line.
//
// Chunks (index = which set/layer, as listed in META):
//   META  UTF-8 JSON, always first.
//   HAIR  one per baked hair set:   HairHeader, u32 pointCounts[strands], f32 positions[points*3],
//                                   f32 widths[points], u8 albedo[points*4] (sRGB, a = 255),
//                                   f32 rootUvs[strands*2], i32 clumpIds[strands],
//                                   f32 randoms[strands]
//   GUID  one per guide layer:      CurvesHeader, u32 pointCounts[curves], f32 positions[points*3]
//   CARD  one per card layer and smoothing (flags bit 0 = smooth):
//                                   CardsHeader, f32 positions[v*3], f32 normals[v*3], f32 uvs[v*2],
//                                   u32 faceSizes[faces], u32 corners[corners]
// Each array starts on an 8-byte boundary inside its payload.

#include <cstddef>
#include <cstdint>

namespace SirBake {

inline constexpr char kMagic[8] = { 'S', 'I', 'R', 'B', 'A', 'K', 'E', '\0' };
inline constexpr uint32_t kFormatVersion = 1;

// Exit codes of `Sisir.exe --bake`.
enum ExitCode : int {
	kExitOk = 0,
	kExitUsage = 2,
	kExitLoadFailed = 3,    // not a readable .sir
	kExitNoMesh = 4,        // the scene's mesh could not be restored
	kExitWriteFailed = 5,   // output or assets directory not writable
	kExitOutOfMemory = 6,
	kExitGenerationFailed = 7
};

#pragma pack(push, 1)
struct FileHeader {
	char magic[8];
	uint32_t formatVersion;
	uint32_t chunkCount;
	uint64_t reserved0;
	uint64_t reserved1;
};
static_assert(sizeof(FileHeader) == 32, "FileHeader layout");

struct ChunkHeader {
	char tag[4];
	uint32_t index;
	uint32_t flags;
	uint32_t crc32;       // of the payload bytes (not the padding)
	uint64_t payloadSize; // bytes, excluding padding
};
static_assert(sizeof(ChunkHeader) == 24, "ChunkHeader layout");

struct HairHeader {
	uint32_t strandCount;
	uint32_t pointCount;
	uint32_t reserved0;
	uint32_t reserved1;
};

struct CurvesHeader {
	uint32_t curveCount;
	uint32_t pointCount;
};

struct CardsHeader {
	uint32_t vertexCount;
	uint32_t faceCount;
	uint32_t cornerCount;
	uint32_t reserved;
};
#pragma pack(pop)

inline constexpr uint32_t kCardFlagSmooth = 1u;

inline constexpr uint64_t align8(uint64_t v) { return (v + 7u) & ~uint64_t(7u); }

// CRC-32 (IEEE 802.3, reflected, as zlib computes it).
struct Crc32Table {
	uint32_t v[256];
	Crc32Table() {
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			v[i] = c;
		}
	}
};

inline uint32_t crc32(const void* data, size_t size, uint32_t crc = 0) {
	static const Crc32Table table; // thread-safe initialisation
	const unsigned char* p = static_cast<const unsigned char*>(data);
	crc = ~crc;
	for (size_t i = 0; i < size; i++) crc = table.v[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
	return ~crc;
}

} // namespace SirBake
