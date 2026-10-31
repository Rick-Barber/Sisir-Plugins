#include "HairBuffers.h"

#include <QByteArray>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace SisirDcc {

namespace {

std::atomic<uint64_t> g_nextRevision{ 1 };

// Scene-file layout, version 1 (inside the zlib block):
//   u32 magic 'SHD1', u32 strandCount, u32 pointCount, u32 reserved,
//   u32 pointCounts[s], f32 positions[p*3], f32 widths[p], u8 albedo[p*4],
//   f32 rootUvs[s*2], i32 clumpIds[s], f32 randoms[s]
constexpr uint32_t kMagic = 0x31444853u; // "SHD1"
// Refuse absurd sizes from a damaged file instead of trying to allocate them.
constexpr uint32_t kMaxStrands = 200u * 1000u * 1000u;
constexpr uint32_t kMaxPoints = 2000u * 1000u * 1000u;

template <typename T>
void append(std::vector<char>& out, const std::vector<T>& v) {
	const size_t bytes = v.size() * sizeof(T);
	const size_t at = out.size();
	out.resize(at + bytes);
	if (bytes) std::memcpy(out.data() + at, v.data(), bytes);
}

template <typename T>
bool take(const char*& p, const char* end, std::vector<T>& v, size_t count) {
	const size_t bytes = count * sizeof(T);
	if ((size_t)(end - p) < bytes) return false;
	v.resize(count);
	if (bytes) std::memcpy(v.data(), p, bytes);
	p += bytes;
	return true;
}

} // namespace

bool HairBuffers::finalize() {
	const size_t strands = pointCounts.size();
	offsets.assign(strands + 1, 0);
	uint64_t total = 0;
	for (size_t s = 0; s < strands; s++) {
		if (pointCounts[s] < 2) return false;
		offsets[s] = (uint32_t)total;
		total += pointCounts[s];
	}
	if (total > kMaxPoints) return false;
	offsets[strands] = (uint32_t)total;
	if (positions.size() != total * 3 || widths.size() != total || albedo.size() != total * 4 ||
	    rootUvs.size() != strands * 2 || clumpIds.size() != strands || randoms.size() != strands) {
		return false;
	}
	float lo[3] = { HUGE_VALF, HUGE_VALF, HUGE_VALF }, hi[3] = { -HUGE_VALF, -HUGE_VALF, -HUGE_VALF };
	for (size_t i = 0; i < total; i++) {
		for (int k = 0; k < 3; k++) {
			const float v = positions[i * 3 + k];
			lo[k] = std::min(lo[k], v);
			hi[k] = std::max(hi[k], v);
		}
	}
	maxWidth = 0.0f;
	for (float w : widths) maxWidth = std::max(maxWidth, w);
	for (int k = 0; k < 3; k++) {
		boundsMin[k] = total ? lo[k] : 0.0f;
		boundsMax[k] = total ? hi[k] : 0.0f;
	}
	revision = g_nextRevision.fetch_add(1, std::memory_order_relaxed);
	return true;
}

std::vector<char> serializeHair(const HairBuffers& b) {
	std::vector<char> raw;
	raw.reserve(16 + b.pointCounts.size() * 4 + b.positions.size() * 4 + b.widths.size() * 4 + b.albedo.size() +
	            b.rootUvs.size() * 4 + b.clumpIds.size() * 4 + b.randoms.size() * 4);
	const uint32_t header[4] = { kMagic, b.strandCount(), b.pointCount(), 0 };
	raw.insert(raw.end(), reinterpret_cast<const char*>(header), reinterpret_cast<const char*>(header) + sizeof(header));
	append(raw, b.pointCounts);
	append(raw, b.positions);
	append(raw, b.widths);
	append(raw, b.albedo);
	append(raw, b.rootUvs);
	append(raw, b.clumpIds);
	append(raw, b.randoms);
	// Level 1: positions and widths barely compress harder at higher levels, and a large groom's
	// save time is dominated by this call.
	const QByteArray packed = qCompress(reinterpret_cast<const uchar*>(raw.data()), (int)std::min<size_t>(raw.size(), INT32_MAX), 1);
	return std::vector<char>(packed.begin(), packed.end());
}

std::shared_ptr<HairBuffers> deserializeHair(const char* data, size_t size) {
	if (!data || size < 4 || size > (size_t)INT32_MAX) return nullptr;
	const QByteArray raw = qUncompress(reinterpret_cast<const uchar*>(data), (int)size);
	if (raw.size() < 16) return nullptr;
	const char* p = raw.constData();
	const char* end = p + raw.size();
	uint32_t header[4];
	std::memcpy(header, p, sizeof(header));
	p += sizeof(header);
	if (header[0] != kMagic || header[1] > kMaxStrands || header[2] > kMaxPoints) return nullptr;
	const size_t s = header[1], n = header[2];
	auto b = std::make_shared<HairBuffers>();
	if (!take(p, end, b->pointCounts, s) || !take(p, end, b->positions, n * 3) || !take(p, end, b->widths, n) ||
	    !take(p, end, b->albedo, n * 4) || !take(p, end, b->rootUvs, s * 2) || !take(p, end, b->clumpIds, s) ||
	    !take(p, end, b->randoms, s)) {
		return nullptr;
	}
	if (!b->finalize()) return nullptr;
	return b;
}

} // namespace SisirDcc
