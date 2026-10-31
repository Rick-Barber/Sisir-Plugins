#include "HairShape.h"
#include "PluginIds.h"

#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnPluginData.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnStringData.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MViewport2Renderer.h>

#include <algorithm>

namespace SisirMaya {

const MTypeId HairShape::id(kHairShapeId);
const MString HairShape::typeName("sisirHairShape");
const MString HairShape::drawDbClassification("drawdb/subscene/sisirHair");
const MString HairShape::drawRegistrantId("SisirHairSubSceneOverride");

MObject HairShape::aHairData;
MObject HairShape::aWidthScale;
MObject HairShape::aDisplayPercent;
MObject HairShape::aRenderPercent;
MObject HairShape::aColorMode;
MObject HairShape::aSetColor;
MObject HairShape::aMaterialId;
MObject HairShape::aGroomRoughness;
MObject HairShape::aGroomAo;
MObject HairShape::aSourceFile;
MObject HairShape::aHairSetName;
MObject HairShape::aStrandCount;
MObject HairShape::aPointCount;

void* HairShape::creator() { return new HairShape(); }

MStatus HairShape::initialize() {
	MFnTypedAttribute typed;
	MFnNumericAttribute num;
	MFnEnumAttribute en;

	aHairData = typed.create("hairData", "hd", HairData::id);
	typed.setStorable(true);
	typed.setHidden(true);
	typed.setConnectable(false);
	addAttribute(aHairData);

	aWidthScale = num.create("widthScale", "wsc", MFnNumericData::kFloat, 1.0);
	num.setMin(0.0);
	num.setSoftMax(4.0);
	num.setKeyable(true);
	addAttribute(aWidthScale);

	aDisplayPercent = num.create("displayPercent", "dpc", MFnNumericData::kFloat, 100.0);
	num.setMin(0.0);
	num.setMax(100.0);
	addAttribute(aDisplayPercent);

	aRenderPercent = num.create("renderPercent", "rpc", MFnNumericData::kFloat, 100.0);
	num.setMin(0.0);
	num.setMax(100.0);
	addAttribute(aRenderPercent);

	// Auto: Sisir's colours while the hair wears its Sisir hair shader, the assigned material's
	// colour once another one is assigned -- so assigning a material shows in the viewport.
	aColorMode = en.create("colorMode", "cmo", 0);
	en.addField("Auto", 0);
	en.addField("Sisir Color", 1);
	en.addField("Hair Set Color", 2);
	en.addField("Material Color", 3);
	addAttribute(aColorMode);

	aSetColor = num.createColor("setColor", "sco");
	num.setDefault(0.9f, 0.8f, 0.65f);
	addAttribute(aSetColor);

	aMaterialId = num.create("sisirMaterialId", "smi", MFnNumericData::kInt, 0);
	num.setMin(0);
	num.setMax(9);
	addAttribute(aMaterialId);

	// What File > Export Hair (ABC) in Sisir writes for the set; hidden, set by the import.
	aGroomRoughness = num.create("sisirGroomRoughness", "sgr", MFnNumericData::kFloat, 0.05);
	num.setMin(0.0);
	num.setMax(1.0);
	num.setHidden(true);
	addAttribute(aGroomRoughness);
	aGroomAo = num.create("sisirGroomAo", "sga", MFnNumericData::kFloat, 1.0);
	num.setMin(0.0);
	num.setMax(1.0);
	num.setHidden(true);
	addAttribute(aGroomAo);

	MFnStringData strings;
	aSourceFile = typed.create("sisirSourceFile", "ssf", MFnData::kString, strings.create(""));
	typed.setUsedAsFilename(true);
	addAttribute(aSourceFile);
	aHairSetName = typed.create("sisirHairSet", "shs", MFnData::kString, strings.create(""));
	addAttribute(aHairSetName);

	// Informational, computed from the hair data, shown in the Attribute Editor.
	aStrandCount = num.create("strandCount", "stc", MFnNumericData::kInt, 0);
	num.setWritable(false);
	num.setStorable(false);
	addAttribute(aStrandCount);
	aPointCount = num.create("pointCount", "ptc", MFnNumericData::kInt, 0);
	num.setWritable(false);
	num.setStorable(false);
	addAttribute(aPointCount);
	attributeAffects(aHairData, aStrandCount);
	attributeAffects(aHairData, aPointCount);
	return MS::kSuccess;
}

void HairShape::postConstructor() {
	setRenderable(true);
}

std::shared_ptr<const HairBuffers> HairShape::buffers() const {
	MPlug plug(thisMObject(), aHairData);
	MObject value;
	if (!plug.getValue(value) || value.isNull()) return nullptr;
	MFnPluginData fn(value);
	const MPxData* data = fn.constData();
	if (!data || data->typeId() != HairData::id) return nullptr;
	return static_cast<const HairData*>(data)->buffers();
}

MStatus HairShape::setBuffers(std::shared_ptr<const HairBuffers> buffers) {
	MFnPluginData fn;
	MStatus status;
	MObject value = fn.create(HairData::id, &status);
	if (!status) return status;
	auto* data = static_cast<HairData*>(fn.data(&status));
	if (!data) return MS::kFailure;
	data->setBuffers(buffers);
	status = MPlug(thisMObject(), aHairData).setValue(value);
	if (!status) return status;
	m_drawRevision++;
	childChanged(kBoundingBoxChanged);
	MHWRender::MRenderer::setGeometryDrawDirty(thisMObject());
	return MS::kSuccess;
}

MStatus HairShape::compute(const MPlug& plug, MDataBlock& data) {
	if (plug != aStrandCount && plug != aPointCount) return MS::kUnknownParameter;
	MDataHandle in = data.inputValue(aHairData);
	const MPxData* raw = in.asPluginData();
	std::shared_ptr<const HairBuffers> b;
	if (raw && raw->typeId() == HairData::id) b = static_cast<const HairData*>(raw)->buffers();
	data.outputValue(aStrandCount).set(b ? (int)b->strandCount() : 0);
	data.outputValue(aPointCount).set(b ? (int)b->pointCount() : 0);
	data.setClean(aStrandCount);
	data.setClean(aPointCount);
	return MS::kSuccess;
}

MBoundingBox HairShape::boundingBox() const {
	auto b = buffers();
	if (!b || b->pointCount() == 0) return MBoundingBox(MPoint(-0.5, -0.5, -0.5), MPoint(0.5, 0.5, 0.5));
	float widthScale = 1.0f;
	MPlug(thisMObject(), aWidthScale).getValue(widthScale);
	const double pad = 0.5 * (double)b->maxWidth * std::max(widthScale, 0.0f);
	return MBoundingBox(MPoint(b->boundsMin[0] - pad, b->boundsMin[1] - pad, b->boundsMin[2] - pad),
	                    MPoint(b->boundsMax[0] + pad, b->boundsMax[1] + pad, b->boundsMax[2] + pad));
}

MStatus HairShape::setDependentsDirty(const MPlug& plug, MPlugArray& affected) {
	const MObject attr = plug.attribute();
	if (attr == aHairData || attr == aWidthScale || attr == aDisplayPercent || attr == aColorMode ||
	    attr == aSetColor || (plug.isChild() && plug.parent().attribute() == aSetColor)) {
		m_drawRevision++;
		MHWRender::MRenderer::setGeometryDrawDirty(thisMObject());
		if (attr == aHairData || attr == aWidthScale) childChanged(kBoundingBoxChanged);
	}
	return MPxSurfaceShape::setDependentsDirty(plug, affected);
}

} // namespace SisirMaya
