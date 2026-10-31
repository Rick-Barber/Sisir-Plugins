#include "SirBakeReader.h"

#include <QJsonDocument>
#include <QJsonParseError>

#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace SirBake {

struct Reader::Mapping {
	HANDLE file = INVALID_HANDLE_VALUE;
	HANDLE mapping = nullptr;
	const char* data = nullptr;
	uint64_t size = 0;
	~Mapping() {
		if (data) UnmapViewOfFile(data);
		if (mapping) CloseHandle(mapping);
		if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
	}
};

namespace {

// Walks one payload's arrays, each starting on an 8-byte boundary.
class Cursor {
public:
	Cursor(const char* base, uint64_t size) : m_base(base), m_size(size) {}
	template <typename T>
	bool header(T& out) {
		if (m_pos + sizeof(T) > m_size) return false;
		std::memcpy(&out, m_base + m_pos, sizeof(T));
		m_pos += sizeof(T);
		return true;
	}
	template <typename T>
	bool array(const T*& out, uint64_t count) {
		m_pos = align8(m_pos);
		const uint64_t bytes = count * sizeof(T);
		if (count > (UINT64_MAX / sizeof(T)) || m_pos + bytes > m_size) return false;
		out = reinterpret_cast<const T*>(m_base + m_pos);
		m_pos += bytes;
		return true;
	}

private:
	const char* m_base;
	uint64_t m_size;
	uint64_t m_pos = 0;
};

bool sumMatches(const uint32_t* counts, uint32_t n, uint64_t expected) {
	uint64_t sum = 0;
	for (uint32_t i = 0; i < n; i++) sum += counts[i];
	return sum == expected;
}

} // namespace

Reader::Reader() = default;
Reader::~Reader() = default;

void Reader::close() {
	m_hair.clear();
	m_guides.clear();
	m_cards.clear();
	m_meta = QJsonObject();
	m_map.reset();
}

bool Reader::open(const QString& path, QString* error) {
	close();
	auto failWith = [&](const QString& why) {
		if (error) *error = why;
		close();
		return false;
	};

	auto map = std::make_unique<Mapping>();
	map->file = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ, nullptr,
	                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	if (map->file == INVALID_HANDLE_VALUE) return failWith("cannot open " + path);
	LARGE_INTEGER size{};
	if (!GetFileSizeEx(map->file, &size) || size.QuadPart < (LONGLONG)sizeof(FileHeader)) return failWith("not a Sisir bake file");
	map->size = (uint64_t)size.QuadPart;
	map->mapping = CreateFileMappingW(map->file, nullptr, PAGE_READONLY, 0, 0, nullptr);
	if (!map->mapping) return failWith("cannot map " + path);
	map->data = static_cast<const char*>(MapViewOfFile(map->mapping, FILE_MAP_READ, 0, 0, 0));
	if (!map->data) return failWith("cannot map " + path);
	m_map = std::move(map);
	const char* data = m_map->data;
	const uint64_t fileSize = m_map->size;

	FileHeader header;
	std::memcpy(&header, data, sizeof(header));
	if (std::memcmp(header.magic, kMagic, 8) != 0) return failWith("not a Sisir bake file");
	// Says which side to update: a plug-in older than Sisir is updated from Sisir's Plugins menu.
	if (header.formatVersion > kFormatVersion) {
		return failWith("this Sisir is newer than its plug-in: update the plug-in in Sisir, Plugins menu, then restart");
	}
	if (header.formatVersion < kFormatVersion) {
		return failWith("this Sisir is older than its plug-in: update Sisir");
	}

	uint64_t pos = sizeof(FileHeader);
	bool haveMeta = false;
	for (uint32_t i = 0; i < header.chunkCount; i++) {
		if (pos + sizeof(ChunkHeader) > fileSize) return failWith("bake file is truncated");
		ChunkHeader ch;
		std::memcpy(&ch, data + pos, sizeof(ch));
		pos += sizeof(ch);
		if (ch.payloadSize > fileSize - pos) return failWith("bake file is truncated");
		const char* payload = data + pos;
		if (crc32(payload, (size_t)ch.payloadSize) != ch.crc32) return failWith("bake file is corrupt (checksum mismatch)");
		pos += align8(ch.payloadSize);
		Cursor cur(payload, ch.payloadSize);
		const std::string tag(ch.tag, 4);
		if (tag == "META") {
			QJsonParseError pe{};
			const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(payload, (int)ch.payloadSize), &pe);
			if (pe.error != QJsonParseError::NoError || !doc.isObject()) return failWith("bake metadata is unreadable");
			m_meta = doc.object();
			haveMeta = true;
		} else if (tag == "HAIR") {
			HairHeader h{};
			HairView v;
			if (!cur.header(h)) return failWith("hair chunk is malformed");
			v.strandCount = h.strandCount;
			v.pointCount = h.pointCount;
			if (!cur.array(v.pointCounts, h.strandCount) || !cur.array(v.positions, (uint64_t)h.pointCount * 3) ||
			    !cur.array(v.widths, h.pointCount) || !cur.array(v.albedo, (uint64_t)h.pointCount * 4) ||
			    !cur.array(v.rootUvs, (uint64_t)h.strandCount * 2) || !cur.array(v.clumpIds, h.strandCount) ||
			    !cur.array(v.randoms, h.strandCount) || !sumMatches(v.pointCounts, h.strandCount, h.pointCount)) {
				return failWith("hair chunk is malformed");
			}
			for (uint32_t s = 0; s < h.strandCount; s++) {
				if (v.pointCounts[s] < 2) return failWith("hair chunk is malformed");
			}
			m_hair.emplace_back(ch.index, v);
		} else if (tag == "GUID") {
			CurvesHeader h{};
			CurvesView v;
			if (!cur.header(h)) return failWith("guide chunk is malformed");
			v.curveCount = h.curveCount;
			v.pointCount = h.pointCount;
			if (!cur.array(v.pointCounts, h.curveCount) || !cur.array(v.positions, (uint64_t)h.pointCount * 3) ||
			    !sumMatches(v.pointCounts, h.curveCount, h.pointCount)) {
				return failWith("guide chunk is malformed");
			}
			m_guides.emplace_back(ch.index, v);
		} else if (tag == "CARD") {
			CardsHeader h{};
			CardsView v;
			if (!cur.header(h)) return failWith("card chunk is malformed");
			v.vertexCount = h.vertexCount;
			v.faceCount = h.faceCount;
			v.cornerCount = h.cornerCount;
			if (!cur.array(v.positions, (uint64_t)h.vertexCount * 3) || !cur.array(v.normals, (uint64_t)h.vertexCount * 3) ||
			    !cur.array(v.uvs, (uint64_t)h.vertexCount * 2) || !cur.array(v.faceSizes, h.faceCount) ||
			    !cur.array(v.corners, h.cornerCount) || !sumMatches(v.faceSizes, h.faceCount, h.cornerCount)) {
				return failWith("card chunk is malformed");
			}
			for (uint32_t f = 0; f < h.faceCount; f++) {
				if (v.faceSizes[f] < 3) return failWith("card chunk is malformed");
			}
			for (uint32_t c = 0; c < h.cornerCount; c++) {
				if (v.corners[c] >= h.vertexCount) return failWith("card chunk is malformed");
			}
			const uint64_t key = ((uint64_t)ch.index << 1) | ((ch.flags & kCardFlagSmooth) ? 1u : 0u);
			m_cards.emplace_back(key, v);
		}
		// Unknown tags are skipped: a newer Sisir may add chunks an older plugin does not need.
	}
	if (!haveMeta) return failWith("bake file has no metadata");
	return true;
}

const HairView* Reader::hair(uint32_t index) const {
	for (const auto& e : m_hair) if (e.first == index) return &e.second;
	return nullptr;
}

const CurvesView* Reader::guides(uint32_t index) const {
	for (const auto& e : m_guides) if (e.first == index) return &e.second;
	return nullptr;
}

const CardsView* Reader::cards(uint32_t index, bool smooth) const {
	const uint64_t key = ((uint64_t)index << 1) | (smooth ? 1u : 0u);
	for (const auto& e : m_cards) if (e.first == key) return &e.second;
	return nullptr;
}

} // namespace SirBake
