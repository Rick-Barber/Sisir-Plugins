#pragma once

// The Alembic groom writer behind File > Export Hair (ABC), with no dependency on Scene: Sisir's
// exporter (ExportAbc.cpp) and the DCC plug-ins (plugins/maya) both feed it, so an .abc written
// from Maya has exactly the layout, names and attribute scopes Sisir writes.
//
// `path` is passed to Alembic as is: UTF-8 in Sisir (its process code page is UTF-8, see
// PathUtf8.h); callers in other processes hand it a path their code page can open.

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace ExportAbc {

// Unreal Engine caps hair strands at 255 control points.  The limit predates UE 5.3, but from
// 5.3 onward the groom importer trims strands that exceed it instead of tolerating them.
// Advisory only -- Maya, Blender and Houdini have no such cap, so nothing here clamps to this
// value; it exists to drive the export warning and the inspector note.
constexpr int kUnrealMaxPointsPerStrand = 255;

// Which groom attributes land in the file, and whether the curves are rotated for Unreal.
//
// The attribute scopes below are not a free choice: UE's groom importer only picks up
// groom_color/groom_roughness/groom_ao from vertex-scope params (FGroomBuilder reads them
// out of VertexAttributes), and only groom_root_uv/groom_clumpid from uniform scope.  A
// value written at the wrong scope is parsed and then silently dropped.
struct Options {
	bool rootUv = true;    // groom_root_uv,   uniform V2f
	bool thickness = true; // the OCurves widths param (not an arbGeomParam), vertex float
	bool clumpId = true;   // groom_clumpid,   uniform int32
	bool color = false;    // groom_color,     vertex V3f
	bool roughness = false; // groom_roughness, vertex float
	bool ao = false;       // groom_ao,        vertex float

	// Bake Sisir's Y-up curves into UE's Z-up space: (x, y, z) -> (x, z, y).  This is what
	// UE's own groom import conversion of Rotation (-90, 0, 0) with Scale (1, 1, -1) works
	// out to, so with this on the user leaves those import settings at their defaults.
	bool unrealOrientation = false;
};

// One hair set as it goes into the file: final units and orientation, invalid strands already
// dropped. Arrays for attributes the Options leave out stay empty.
struct CurveSet {
	std::string name;                // sanitized and made unique by the writer
	std::vector<float> positions;    // xyz per point
	std::vector<int32_t> pointCounts; // per curve
	std::vector<float> widths;       // per point
	std::vector<float> rootUvs;      // uv per curve
	std::vector<int32_t> clumpIds;   // per curve
	std::vector<float> colors;       // rgb per point
	std::vector<float> roughnesses;  // per point
	std::vector<float> aos;          // per point
};

class Writer {
public:
	Writer();
	~Writer();
	Writer(const Writer&) = delete;
	Writer& operator=(const Writer&) = delete;

	// False with `error` set when the archive cannot be created.
	bool open(const std::string& path, std::string* error);
	// One OCurves object at the top of the archive, named after the set (`index` names an
	// unnamed one). An attribute whose array length disagrees with the curves is left out and
	// reported in `warnings`. False with `error` set when Alembic rejects the set.
	bool addCurveSet(const CurveSet& set, size_t index, const Options& options,
	                 std::vector<std::string>* warnings, std::string* error);
	// Flushes and closes the archive (also done by the destructor).
	void close();

private:
	struct Archive;
	std::unique_ptr<Archive> m_archive;
	std::unordered_set<std::string> m_usedNames;
};

} // namespace ExportAbc
