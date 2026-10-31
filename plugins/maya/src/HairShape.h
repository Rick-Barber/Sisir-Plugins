#pragma once

// sisirHairShape: one Sisir hair set inside Maya. Holds the strands (HairData) and the few
// attributes an artist adjusts after import; the viewport draws it through HairDrawOverride and
// Arnold renders it through the SisirMtoA translator.

#include "HairData.h"

#include <maya/MPxSurfaceShape.h>

namespace SisirMaya {

class HairShape : public MPxSurfaceShape {
public:
	static const MTypeId id;
	static const MString typeName;
	static const MString drawDbClassification;
	static const MString drawRegistrantId;

	static MObject aHairData;
	static MObject aWidthScale;
	static MObject aDisplayPercent;
	static MObject aRenderPercent;
	static MObject aColorMode;   // 0 Sisir colour (per point), 1 hair set colour
	static MObject aSetColor;
	static MObject aMaterialId;
	static MObject aSourceFile;
	static MObject aHairSetName;
	static MObject aGroomRoughness; // the hair set's Sisir material, for groom_roughness in an ABC
	static MObject aGroomAo;        // and for groom_ao
	static MObject aStrandCount;
	static MObject aPointCount;

	static void* creator();
	static MStatus initialize();

	void postConstructor() override;
	bool isBounded() const override { return true; }
	MBoundingBox boundingBox() const override;
	MStatus setDependentsDirty(const MPlug& plug, MPlugArray& affected) override;
	MStatus compute(const MPlug& plug, MDataBlock& data) override;

	// The strands on this node, or null. Reading never evaluates the graph beyond this node.
	std::shared_ptr<const HairBuffers> buffers() const;
	// Installs new strands and refreshes the informational counts.
	MStatus setBuffers(std::shared_ptr<const HairBuffers> buffers);

	// Bumped whenever something the viewport draws changes; the draw override compares it.
	uint64_t drawRevision() const { return m_drawRevision; }

private:
	uint64_t m_drawRevision = 1;
};

} // namespace SisirMaya
