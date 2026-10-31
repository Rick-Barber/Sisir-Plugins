#pragma once

// The strands of one hair set, as a Maya data type: what sisirHairShape stores and saves.
//
// The arrays live in an immutable, shared HairBuffers (plugins/common). Maya copies data objects
// freely (every plug read hands out a copy); with shared buffers those copies cost a pointer.

#include "DccShared.h"

#include <maya/MBoundingBox.h>
#include <maya/MPxGeometryData.h>
#include <maya/MString.h>
#include <maya/MTypeId.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace SisirMaya {

class HairData : public MPxGeometryData {
public:
	static const MTypeId id;
	static const MString typeName;
	static void* creator();

	HairData() = default;

	std::shared_ptr<const HairBuffers> buffers() const { return m_buffers; }
	void setBuffers(std::shared_ptr<const HairBuffers> buffers) { m_buffers = std::move(buffers); }

	// MPxData
	MStatus readASCII(const MArgList& args, unsigned int& lastElement) override;
	MStatus readBinary(std::istream& in, unsigned int length) override;
	MStatus writeASCII(std::ostream& out) override;
	MStatus writeBinary(std::ostream& out) override;
	void copy(const MPxData& src) override;
	MTypeId typeId() const override { return id; }
	MString name() const override { return typeName; }

	// MPxGeometryData: no component editing; the iterator is never asked for.
	MPxGeometryIterator* iterator(MObjectArray&, MObject&, bool) override { return nullptr; }
	MPxGeometryIterator* iterator(MObjectArray&, MObject&, bool, bool) const override { return nullptr; }
	bool updateCompleteVertexGroup(MObject&) const override { return false; }

private:
	std::shared_ptr<const HairBuffers> m_buffers;
};

} // namespace SisirMaya
