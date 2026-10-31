#pragma once

// Reads a .sirbake (src/SirBakeFormat.h) for the DCC plugins.
//
// The file is memory-mapped and validated once on open -- magic, version, every chunk's bounds and
// CRC, every array against its header's counts -- so the views handed out afterwards point straight
// into the mapping with nothing left to check. Needs QtCore only (for META's JSON), which every DCC
// plugin already links through its host's Qt.

#include "SirBakeFormat.h"

#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

namespace SirBake {

struct HairView {
	uint32_t strandCount = 0;
	uint32_t pointCount = 0;
	const uint32_t* pointCounts = nullptr; // [strandCount]
	const float* positions = nullptr;      // [pointCount * 3], metres
	const float* widths = nullptr;         // [pointCount], full width, metres
	const uint8_t* albedo = nullptr;       // [pointCount * 4], sRGB
	const float* rootUvs = nullptr;        // [strandCount * 2]
	const int32_t* clumpIds = nullptr;     // [strandCount]
	const float* randoms = nullptr;        // [strandCount]
};

struct CurvesView {
	uint32_t curveCount = 0;
	uint32_t pointCount = 0;
	const uint32_t* pointCounts = nullptr;
	const float* positions = nullptr;
};

struct CardsView {
	uint32_t vertexCount = 0;
	uint32_t faceCount = 0;
	uint32_t cornerCount = 0;
	const float* positions = nullptr; // [v * 3]
	const float* normals = nullptr;   // [v * 3]
	const float* uvs = nullptr;       // [v * 2], bottom-left origin
	const uint32_t* faceSizes = nullptr;
	const uint32_t* corners = nullptr;
};

class Reader {
public:
	Reader();
	~Reader();
	Reader(const Reader&) = delete;
	Reader& operator=(const Reader&) = delete;

	// False with a readable reason on any inconsistency; the reader is then empty.
	bool open(const QString& path, QString* error);
	void close();

	const QJsonObject& meta() const { return m_meta; }
	// Chunk index as META lists it ("chunk"); null when the file has no such chunk.
	const HairView* hair(uint32_t index) const;
	const CurvesView* guides(uint32_t index) const;
	const CardsView* cards(uint32_t index, bool smooth) const;

private:
	struct Mapping;
	std::unique_ptr<Mapping> m_map;
	QJsonObject m_meta;
	std::vector<std::pair<uint32_t, HairView>> m_hair;
	std::vector<std::pair<uint32_t, CurvesView>> m_guides;
	std::vector<std::pair<uint64_t, CardsView>> m_cards; // key = index << 1 | smooth
};

} // namespace SirBake
