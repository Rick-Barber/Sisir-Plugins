#include "ExportAbcWriter.h"

#include <Alembic/AbcGeom/All.h>
#include <Alembic/AbcCoreOgawa/All.h>

#include <cctype>
#include <optional>

namespace ExportAbc {

namespace {

std::string sanitizeName(const std::string& name, size_t index) {
	std::string out = name;
	for (char& c : out) {
		if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
			c = '_';
		}
	}
	if (out.empty()) {
		out = "HairSet_" + std::to_string(index + 1u);
	}
	return out;
}

std::string makeUniqueName(const std::string& baseName, std::unordered_set<std::string>& usedNames) {
	std::string uniqueName = baseName;
	int suffix = 2;
	while (!usedNames.insert(uniqueName).second) {
		uniqueName = baseName + "_" + std::to_string(suffix++);
	}
	return uniqueName;
}

} // namespace

struct Writer::Archive {
	std::optional<Alembic::Abc::OArchive> archive;
};

Writer::Writer() = default;

Writer::~Writer() { close(); }

bool Writer::open(const std::string& path, std::string* error) {
	close();
	m_usedNames.clear();
	try {
		m_archive = std::make_unique<Archive>();
		m_archive->archive.emplace(Alembic::AbcCoreOgawa::WriteArchive(), path);
		return true;
	} catch (const std::exception& ex) {
		m_archive.reset();
		if (error) *error = ex.what();
		return false;
	}
}

void Writer::close() {
	// The archive is flushed when OArchive is destroyed.
	m_archive.reset();
}

bool Writer::addCurveSet(const CurveSet& set, size_t index, const Options& options,
                         std::vector<std::string>* warnings, std::string* error) {
	using namespace Alembic::AbcGeom;
	using Alembic::Abc::Int32ArraySample;
	using Alembic::Abc::P3fArraySample;
	using Alembic::Abc::FloatArraySample;
	using Alembic::Abc::V2fArraySample;
	using Alembic::Abc::V3fArraySample;

	if (!m_archive || !m_archive->archive) {
		if (error) *error = "the archive is not open";
		return false;
	}
	try {
		const size_t curveCount = set.pointCounts.size();
		const size_t pointCount = set.positions.size() / 3u;
		auto warnCountMismatch = [&](const char* attribute, size_t got, size_t expected) {
			if (warnings) {
				warnings->push_back(std::string(attribute) + " count mismatch: got " + std::to_string(got) +
				                    ", expected " + std::to_string(expected) + "; skipping");
			}
		};

		std::string name = makeUniqueName(sanitizeName(set.name, index), m_usedNames);
		OCurves curves(m_archive->archive->getTop(), name);
		OCurvesSchema& schema = curves.getSchema();
		P3fArraySample posSample(reinterpret_cast<const Imath::V3f*>(set.positions.data()), pointCount);
		Int32ArraySample vertsSample(set.pointCounts);
		// An empty sample leaves the widths slot out of the file entirely, the same idiom
		// the UV and normal slots below already use.
		OFloatGeomParam::Sample widthsSample;
		if (options.thickness) {
			if (set.widths.size() == pointCount) {
				widthsSample = OFloatGeomParam::Sample(FloatArraySample(set.widths), kVertexScope);
			} else {
				warnCountMismatch("widths", set.widths.size(), pointCount);
			}
		}
		OCurvesSchema::Sample sampleData(
			posSample,
			vertsSample,
			kLinear,
			kNonPeriodic,
			widthsSample,
			OV2fGeomParam::Sample(),
			ON3fGeomParam::Sample(),
			kNoBasis);
		// Keep these explicit at the serialization boundary. Some readers use
		// the basis alone to choose the spline representation, so a default
		// Bezier basis would also corrupt the point-to-width correspondence.
		sampleData.setType(kLinear);
		sampleData.setWrap(kNonPeriodic);
		sampleData.setBasis(kNoBasis);
		schema.set(sampleData);

		Alembic::Abc::OCompoundProperty arbParams = schema.getArbGeomParams();

		// Every groom_* attribute below has exactly one scope UE will read it at; see Options.
		// Each is guarded on its buffer length so a bookkeeping slip drops the attribute with a
		// warning instead of writing a corrupt correspondence.

		// UE5's groom importer reads per-strand root UVs from this uniform-scope
		// arbGeomParam; without it, it falls back to a generated spherical mapping.
		// The schema's own UV slot above is vertex/varying scope and is not it.
		if (options.rootUv) {
			if (set.rootUvs.size() == curveCount * 2u) {
				OV2fGeomParam rootUvParam(arbParams, "groom_root_uv", false, kUniformScope, 1);
				rootUvParam.set(OV2fGeomParam::Sample(
					V2fArraySample(reinterpret_cast<const Imath::V2f*>(set.rootUvs.data()), curveCount), kUniformScope));
			} else {
				warnCountMismatch("groom_root_uv", set.rootUvs.size() / 2u, curveCount);
			}
		}

		if (options.clumpId) {
			if (set.clumpIds.size() == curveCount) {
				OInt32GeomParam clumpParam(arbParams, "groom_clumpid", false, kUniformScope, 1);
				clumpParam.set(OInt32GeomParam::Sample(Int32ArraySample(set.clumpIds), kUniformScope));
			} else {
				warnCountMismatch("groom_clumpid", set.clumpIds.size(), curveCount);
			}
		}

		if (options.color) {
			if (set.colors.size() == pointCount * 3u) {
				OV3fGeomParam colorParam(arbParams, "groom_color", false, kVertexScope, 1);
				colorParam.set(OV3fGeomParam::Sample(
					V3fArraySample(reinterpret_cast<const Imath::V3f*>(set.colors.data()), pointCount), kVertexScope));
			} else {
				warnCountMismatch("groom_color", set.colors.size() / 3u, pointCount);
			}
		}

		if (options.roughness) {
			if (set.roughnesses.size() == pointCount) {
				OFloatGeomParam roughnessParam(arbParams, "groom_roughness", false, kVertexScope, 1);
				roughnessParam.set(OFloatGeomParam::Sample(FloatArraySample(set.roughnesses), kVertexScope));
			} else {
				warnCountMismatch("groom_roughness", set.roughnesses.size(), pointCount);
			}
		}

		if (options.ao) {
			if (set.aos.size() == pointCount) {
				OFloatGeomParam aoParam(arbParams, "groom_ao", false, kVertexScope, 1);
				aoParam.set(OFloatGeomParam::Sample(FloatArraySample(set.aos), kVertexScope));
			} else {
				warnCountMismatch("groom_ao", set.aos.size(), pointCount);
			}
		}
		return true;
	} catch (const std::exception& ex) {
		if (error) *error = ex.what();
		return false;
	} catch (...) {
		if (error) *error = "unknown error";
		return false;
	}
}

} // namespace ExportAbc
