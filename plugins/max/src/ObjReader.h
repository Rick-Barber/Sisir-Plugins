#pragma once

// A small, strict OBJ reader for the meshes in a bake (always OBJ, BakeExport.cpp bakeMesh): the
// user's file as authored, or Sisir's own geometry. Max's own OBJ importer is not used on purpose:
// its presets (axis, scale, normals, which the user may have changed) would make the import differ
// from machine to machine. Polygons are kept as they are; one object per `o` / `g`, as Maya's OBJ
// import with multiple objects gives. `mtllib` / `usemtl` are ignored.

#include <QString>

#include <cstdint>
#include <vector>

namespace SisirMax {

struct ObjObject {
	QString name;
	std::vector<float> positions;   // xyz per vertex, file units, as in the file (Y-up)
	std::vector<float> uvs;         // uv per uv vertex
	std::vector<float> normals;     // xyz per normal
	std::vector<uint32_t> faceSizes;
	std::vector<uint32_t> faceVerts;   // per corner
	std::vector<int32_t> faceUvs;      // per corner, -1: none
	std::vector<int32_t> faceNormals;  // per corner, -1: none
	bool hasUvs = false;
	bool hasNormals = false;
};

// False with `error` when the file cannot be read; objects with no faces are dropped.
bool readObj(const QString& path, std::vector<ObjObject>& objects, QString* error);

} // namespace SisirMax
