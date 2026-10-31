#include "ObjReader.h"

#include <QFile>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <unordered_map>

namespace SisirMax {

namespace {

const char* skipSpace(const char* p, const char* end) {
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	return p;
}

bool parseFloat(const char*& p, const char* end, float& out) {
	p = skipSpace(p, end);
	const auto r = std::from_chars(p, end, out);
	if (r.ec != std::errc()) return false;
	p = r.ptr;
	return true;
}

bool parseInt(const char*& p, const char* end, long long& out) {
	const auto r = std::from_chars(p, end, out);
	if (r.ec != std::errc()) return false;
	p = r.ptr;
	return true;
}

// OBJ indices are global, 1-based, negative ones counting back; each object keeps only the
// vertices its faces use, renumbered.
struct Remap {
	std::unordered_map<long long, int32_t> index;
	int32_t get(long long global, const std::vector<float>& source, int dim, std::vector<float>& target) {
		auto it = index.find(global);
		if (it != index.end()) return it->second;
		const int32_t local = (int32_t)(target.size() / dim);
		target.insert(target.end(), source.begin() + (size_t)global * dim, source.begin() + (size_t)global * dim + dim);
		index.emplace(global, local);
		return local;
	}
};

} // namespace

bool readObj(const QString& path, std::vector<ObjObject>& objects, QString* error) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) *error = "cannot read " + path;
		return false;
	}
	const QByteArray data = file.readAll();
	const char* p = data.constData();
	const char* end = p + data.size();

	std::vector<float> positions, uvs, normals;
	objects.clear();
	objects.emplace_back();
	Remap vRemap, tRemap, nRemap;
	QString groupName;
	auto startObject = [&](const QString& name) {
		if (!objects.back().faceSizes.empty()) {
			objects.emplace_back();
			vRemap = Remap();
			tRemap = Remap();
			nRemap = Remap();
		}
		objects.back().name = name;
	};

	int lineNumber = 0;
	while (p < end) {
		const char* lineEnd = static_cast<const char*>(std::memchr(p, '\n', (size_t)(end - p)));
		if (!lineEnd) lineEnd = end;
		const char* e = lineEnd;
		if (e > p && e[-1] == '\r') e--;
		lineNumber++;
		const char* q = skipSpace(p, e);
		p = lineEnd + 1;
		if (q >= e || *q == '#') continue;
		auto keyword = [&](const char* k) {
			const size_t n = std::strlen(k);
			return (size_t)(e - q) >= n && std::memcmp(q, k, n) == 0 && ((size_t)(e - q) == n || q[n] == ' ' || q[n] == '\t');
		};
		if (keyword("v")) {
			q += 1;
			float x, y, z;
			if (!parseFloat(q, e, x) || !parseFloat(q, e, y) || !parseFloat(q, e, z)) continue;
			positions.insert(positions.end(), { x, y, z });
		} else if (keyword("vt")) {
			q += 2;
			float u, v = 0.0f;
			if (!parseFloat(q, e, u)) continue;
			parseFloat(q, e, v);
			uvs.insert(uvs.end(), { u, v });
		} else if (keyword("vn")) {
			q += 2;
			float x, y, z;
			if (!parseFloat(q, e, x) || !parseFloat(q, e, y) || !parseFloat(q, e, z)) continue;
			normals.insert(normals.end(), { x, y, z });
		} else if (keyword("o") || keyword("g")) {
			const QString name = QString::fromUtf8(q + 1, (int)(e - q - 1)).trimmed();
			if (!name.isEmpty() && name != objects.back().name) startObject(name);
		} else if (keyword("f")) {
			q += 1;
			ObjObject& o = objects.back();
			uint32_t corners = 0;
			while (true) {
				q = skipSpace(q, e);
				if (q >= e) break;
				long long vi = 0, ti = 0, ni = 0;
				if (!parseInt(q, e, vi)) break;
				bool hasT = false, hasN = false;
				if (q < e && *q == '/') {
					q++;
					if (q < e && *q != '/') hasT = parseInt(q, e, ti);
					if (q < e && *q == '/') {
						q++;
						hasN = parseInt(q, e, ni);
					}
				}
				auto resolve = [](long long i, size_t count) { return i < 0 ? (long long)count + i : i - 1; };
				vi = resolve(vi, positions.size() / 3);
				if (vi < 0 || (size_t)vi >= positions.size() / 3) {
					if (error) *error = QString("%1: line %2 refers to a vertex that does not exist").arg(path).arg(lineNumber);
					return false;
				}
				o.faceVerts.push_back((uint32_t)vRemap.get(vi, positions, 3, o.positions));
				int32_t t = -1, n = -1;
				if (hasT) {
					ti = resolve(ti, uvs.size() / 2);
					if (ti >= 0 && (size_t)ti < uvs.size() / 2) t = tRemap.get(ti, uvs, 2, o.uvs);
				}
				if (hasN) {
					ni = resolve(ni, normals.size() / 3);
					if (ni >= 0 && (size_t)ni < normals.size() / 3) n = nRemap.get(ni, normals, 3, o.normals);
				}
				o.faceUvs.push_back(t);
				o.faceNormals.push_back(n);
				corners++;
				// Skip anything left of this corner (a malformed token).
				while (q < e && *q != ' ' && *q != '\t') q++;
			}
			if (corners < 3) {
				o.faceVerts.resize(o.faceVerts.size() - corners);
				o.faceUvs.resize(o.faceUvs.size() - corners);
				o.faceNormals.resize(o.faceNormals.size() - corners);
				continue;
			}
			o.faceSizes.push_back(corners);
		}
		// mtllib, usemtl, s, l, p: nothing the import uses.
	}

	std::vector<ObjObject> kept;
	for (ObjObject& o : objects) {
		if (o.faceSizes.empty()) continue;
		// A face corner without a uv (or normal) in an object where others have them: none for all,
		// as a partial channel cannot be expressed per corner.
		o.hasUvs = !o.uvs.empty() && std::find(o.faceUvs.begin(), o.faceUvs.end(), -1) == o.faceUvs.end();
		o.hasNormals = !o.normals.empty() && std::find(o.faceNormals.begin(), o.faceNormals.end(), -1) == o.faceNormals.end();
		kept.push_back(std::move(o));
	}
	objects = std::move(kept);
	if (objects.empty() && error) *error = path + " holds no faces";
	return !objects.empty();
}

} // namespace SisirMax
