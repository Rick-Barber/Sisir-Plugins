#include "HairDrawOverride.h"

#include "HairShape.h"
#include "HairViewColor.h"
#include "RenderingSpace.h"

#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MHWGeometryUtilities.h>
#include <maya/MSelectionContext.h>
#include <maya/MPlug.h>
#include <maya/MFloatArray.h>
#include <maya/MFloatMatrix.h>
#include <maya/MFloatVectorArray.h>
#include <maya/MPlugArray.h>
#include <maya/MRenderUtil.h>
#include <maya/MSelectionList.h>
#include <maya/MSelectionMask.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace SisirMaya {

using namespace MHWRender;

namespace {
const MString kItemName("sisirHairStrands");
const MString kWireItemName("sisirHairSelectionWire");
// How much of the groom the selection wireframe draws: enough to read the shape, not a solid block.
constexpr float kWirePercent = 20.0f;
MString g_shaderDir;

template <typename T>
T plugValue(const MObject& node, const MObject& attr, T fallback) {
	MPlug plug(node, attr);
	T v = fallback;
	plug.getValue(v);
	return v;
}
// The colour a material shows for the viewport: base colour (or a Lambert's colour), weighted by
// `base`, times aiStandardHair's melanin colour as Arnold renders it (kArnoldMelanin). perPoint: the material's colour IS Sisir's per-point albedo (the hair shader
// the importer makes reads it through aiUserDataColor "sisir_albedo").
struct ViewColor {
	bool perPoint = true;
	float rgb[3] = { 0.8f, 0.8f, 0.8f };
	bool gradient = false;
	float gradientColors[kGradientSamples * 3] = {};
};

// A texture network driving the base colour, sampled along the strand: u 0.5, v from 0 (root) to
// 1 (tip) -- the coordinates Arnold gives a curve, so a ramp reads root to tip in both.
bool sampleAlongStrand(const MString& sourcePlug, float out[kGradientSamples * 3]) {
	// Maya's software evaluator runs Maya's own texture nodes; Arnold-only nodes come back black.
	MSelectionList sel;
	MObject sourceNode;
	if (sel.add(sourcePlug) && sel.getDependNode(0, sourceNode)) {
		const MString type = MFnDependencyNode(sourceNode).typeName();
		if (type.length() > 2 && type.substring(0, 1) == "ai") return false;
	}
	MFloatArray us(kGradientSamples, 0.5f), vs(kGradientSamples);
	for (int i = 0; i < kGradientSamples; i++) vs[i] = (float)i / (float)(kGradientSamples - 1);
	MFloatMatrix camera;
	MFloatVectorArray colors, transparencies;
	if (!MRenderUtil::sampleShadingNetwork(sourcePlug, kGradientSamples, false, false, camera, nullptr, &us, &vs,
	                                       nullptr, nullptr, nullptr, nullptr, nullptr, colors, transparencies) ||
	    colors.length() != (unsigned int)kGradientSamples) {
		return false;
	}
	float peak = 0.0f;
	for (int i = 0; i < kGradientSamples; i++) {
		for (int k = 0; k < 3; k++) {
			out[i * 3 + k] = std::max(0.0f, colors[i][k]);
			peak = std::max(peak, out[i * 3 + k]);
		}
	}
	// All black is what Maya's evaluator returns for nodes it cannot run (Arnold's ai* utilities,
	// other plug-ins' nodes): not a colour, so the caller falls back to the neutral tint.
	return peak > 1.0e-4f;
}

bool readColor(const MFnDependencyNode& fn, const char* name, float out[3], bool* sisirAlbedo, MString* sourcePlug = nullptr) {
	MPlug plug = fn.findPlug(name, true);
	if (plug.isNull() || !plug.isCompound() || plug.numChildren() < 3) return false;
	MPlugArray sources;
	if (plug.connectedTo(sources, true, false) && sources.length() > 0) {
		MFnDependencyNode srcFn(sources[0].node());
		if (srcFn.typeName() == "aiUserDataColor") {
			MString attribute;
			srcFn.findPlug("attribute", true).getValue(attribute);
			if (attribute == "sisir_albedo") {
				if (sisirAlbedo) *sisirAlbedo = true;
				return true;
			}
		}
		if (sourcePlug) *sourcePlug = sources[0].name();
		// Driven by a texture or a network: a neutral value here (the network itself is sampled along
		// the strand by the caller), never the
		// input's default colour (black on most texture nodes, which turned melanin hair near-black).
		// White when the material has melanin, so the melanin colour shows as the render has it;
		// mid-grey for a plain surface colour.
		const bool hasMelanin = !fn.findPlug("melanin", true).isNull();
		out[0] = out[1] = out[2] = hasMelanin ? 1.0f : 0.5f;
		return true;
	}
	for (unsigned int k = 0; k < 3; k++) out[k] = plug.child(k).asFloat();
	return true;
}

ViewColor materialColor(const MDagPath& path) {
	ViewColor vc;
	MFnDagNode shapeFn(path);
	MPlug groups = shapeFn.findPlug("instObjGroups", true);
	if (groups.isNull()) return vc;
	MPlug instance = groups.elementByLogicalIndex(path.instanceNumber());
	MPlugArray dests;
	if (!instance.connectedTo(dests, false, true)) return vc;
	MObject sg;
	for (unsigned int i = 0; i < dests.length(); i++) {
		if (dests[i].node().hasFn(MFn::kShadingEngine)) {
			sg = dests[i].node();
			break;
		}
	}
	if (sg.isNull()) return vc;
	MPlugArray shaders;
	if (!MFnDependencyNode(sg).findPlug("surfaceShader", true).connectedTo(shaders, true, false) || shaders.length() == 0) return vc;
	MFnDependencyNode shader(shaders[0].node());
	float rgb[3] = { 0.8f, 0.8f, 0.8f };
	bool sisir = false;
	MString source;
	if (!readColor(shader, "baseColor", rgb, &sisir, &source) && !readColor(shader, "color", rgb, &sisir, &source)) return vc;
	if (sisir) return vc;
	vc.perPoint = false;
	if (source.length() && sampleAlongStrand(source, vc.gradientColors)) {
		vc.gradient = true;
		rgb[0] = rgb[1] = rgb[2] = 1.0f; // the samples carry the colour; melanin and weight tint them
	}
	MPlug base = shader.findPlug("base", true);
	const float weight = base.isNull() ? 1.0f : base.asFloat();
	MPlug melaninPlug = shader.findPlug("melanin", true);
	if (!melaninPlug.isNull()) {
		MPlug rednessPlug = shader.findPlug("melaninRedness", true);
		const float redness = rednessPlug.isNull() ? 0.5f : rednessPlug.asFloat();
		float tint[3];
		arnoldMelaninColor(melaninPlug.asFloat(), redness, tint);
		for (int k = 0; k < 3; k++) rgb[k] *= tint[k];
	}
	for (int k = 0; k < 3; k++) vc.rgb[k] = std::clamp(rgb[k] * weight, 0.0f, 1.0f);
	return vc;
}

} // namespace

void HairDrawOverride::setShaderDirectory(const MString& dir) { g_shaderDir = dir; }

MPxSubSceneOverride* HairDrawOverride::creator(const MObject& obj) { return new HairDrawOverride(obj); }

HairDrawOverride::HairDrawOverride(const MObject& obj) : MPxSubSceneOverride(obj), m_node(obj) {
	MFnDependencyNode fn(obj);
	m_shape = dynamic_cast<HairShape*>(fn.userNode());
}

HairDrawOverride::~HairDrawOverride() { releaseShader(); }

DrawAPI HairDrawOverride::supportedDrawAPIs() const { return kOpenGL | kDirectX11 | kOpenGLCoreProfile; }

void HairDrawOverride::releaseShader() {
	if (MRenderer* renderer = MRenderer::theRenderer()) {
		if (const MShaderManager* sm = renderer->getShaderManager()) {
			if (m_shader) sm->releaseShader(m_shader);
			if (m_wireShader) sm->releaseShader(m_wireShader);
		}
	}
	m_shader = nullptr;
	m_wireShader = nullptr;
}

bool HairDrawOverride::ensureShader() {
	if (m_shader) return true;
	MRenderer* renderer = MRenderer::theRenderer();
	const MShaderManager* sm = renderer ? renderer->getShaderManager() : nullptr;
	if (!sm) return false;
	static bool pathAdded = false;
	if (!pathAdded && g_shaderDir.length()) {
		sm->addShaderPath(g_shaderDir);
		pathAdded = true;
	}
	m_shader = sm->getEffectsFileShader("SisirHair", "Main");
	m_customShader = m_shader != nullptr;
	if (!m_shader) {
		// The effect failed to compile on this GPU / API: thick coloured lines still show the groom.
		static bool warned = false;
		if (!warned) {
			MGlobal::displayWarning("Sisir: hair viewport shader unavailable, drawing hair as lines");
			warned = true;
		}
		m_shader = sm->getStockShader(MShaderManager::k3dCPVThickLineShader);
		if (m_shader) {
			const float width[2] = { 1.0f, 1.0f };
			m_shader->setParameter("lineWidth", width);
		}
	}
	return m_shader != nullptr;
}

HairDrawOverride::State HairDrawOverride::currentState() const {
	State s;
	if (!m_shape) return s;
	s.drawRevision = m_shape->drawRevision();
	s.displayPercent = plugValue<float>(m_node, HairShape::aDisplayPercent, 100.0f);
	MDagPathArray paths;
	MDagPath::getAllPathsTo(m_node, paths);
	for (unsigned int i = 0; i < paths.length(); i++) {
		s.matrices.push_back(paths[i].inclusiveMatrix());
		s.status.push_back((int)MGeometryUtilities::displayStatus(paths[i]));
		s.visible.push_back(paths[i].isVisible() ? 1 : 0);
	}
	// The colour the strands show, read every frame so assigning or editing a material shows at once.
	const int mode = plugValue<int>(m_node, HairShape::aColorMode, 0);
	if (mode == 2) {
		MPlug c(m_node, HairShape::aSetColor);
		s.flatColor = true;
		for (unsigned int k = 0; k < 3; k++) s.color[k] = c.child(k).asFloat();
	} else if (mode == 0 || mode == 3) {
		const ViewColor vc = paths.length() ? materialColor(paths[0]) : ViewColor();
		s.flatColor = !vc.perPoint || mode == 3;
		for (int k = 0; k < 3; k++) s.color[k] = vc.rgb[k];
		s.gradient = vc.gradient;
		std::memcpy(s.gradientColors, vc.gradientColors, sizeof(s.gradientColors));
	}
	return s;
}

bool HairDrawOverride::requiresUpdate(const MSubSceneContainer&, const MFrameContext&) const {
	const State s = currentState();
	return !m_haveGeometry || s.drawRevision != m_state.drawRevision || s.displayPercent != m_state.displayPercent ||
	       s.matrices != m_state.matrices || s.status != m_state.status || s.visible != m_state.visible ||
	       s.flatColor != m_state.flatColor || std::memcmp(s.color, m_state.color, sizeof(s.color)) != 0 ||
	       s.gradient != m_state.gradient || std::memcmp(s.gradientColors, m_state.gradientColors, sizeof(s.gradientColors)) != 0;
}

void HairDrawOverride::rebuildBuffers(const HairBuffers& b) {
	m_vertexBuffers.clear();
	MVertexBufferDescriptorList required;
	m_shader->requiredVertexBuffers(required);
	const uint32_t n = b.pointCount();
	for (int r = 0; r < required.length(); r++) {
		MVertexBufferDescriptor desc;
		if (!required.getDescriptor(r, desc)) continue;
		auto vb = std::make_unique<MVertexBuffer>(desc);
		const int dim = desc.dimension();
		if (desc.dataType() != MGeometry::kFloat || dim <= 0) continue;
		float* out = static_cast<float*>(vb->acquire(n, true));
		if (!out) continue;
		switch (desc.semantic()) {
		case MGeometry::kPosition:
			for (uint32_t i = 0; i < n; i++) {
				for (int k = 0; k < dim; k++) out[i * dim + k] = k < 3 ? b.positions[i * 3 + k] : 1.0f;
			}
			break;
		case MGeometry::kColor: {
			// Into the rendering space the viewport works in, as the Arnold translator does.
			float m[9];
			renderingSpaceMatrix(m);
			float lut[256];
			for (int c = 0; c < 256; c++) lut[c] = srgbToLinear((uint8_t)c);
			for (uint32_t i = 0; i < n; i++) {
				const float lin[3] = { lut[b.albedo[i * 4]], lut[b.albedo[i * 4 + 1]], lut[b.albedo[i * 4 + 2]] };
				float rs[3];
				applyMatrix(m, lin, rs);
				for (int k = 0; k < dim; k++) out[i * dim + k] = k < 3 ? rs[k] : 1.0f;
			}
			break;
		}
		case MGeometry::kTangent:
			for (uint32_t s = 0; s < b.strandCount(); s++) {
				const uint32_t first = b.offsets[s], count = b.pointCounts[s];
				for (uint32_t j = 0; j < count; j++) {
					const uint32_t a = first + (j > 0 ? j - 1 : 0), c = first + std::min(j + 1, count - 1);
					float t[3];
					for (int k = 0; k < 3; k++) t[k] = b.positions[c * 3 + k] - b.positions[a * 3 + k];
					const float len = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
					for (int k = 0; k < dim; k++) out[(first + j) * dim + k] = (k < 3 && len > 0.0f) ? t[k] / len : 0.0f;
				}
			}
			break;
		case MGeometry::kTexture:
			// x: the strand's width there; y: how far along it, root 0 to tip 1; z: the strand's random.
			for (uint32_t s = 0; s < b.strandCount(); s++) {
				const uint32_t first = b.offsets[s], count = b.pointCounts[s];
				for (uint32_t j = 0; j < count; j++) {
					float* v = out + (size_t)(first + j) * dim;
					for (int k = 0; k < dim; k++) v[k] = 0.0f;
					v[0] = b.widths[first + j];
					if (dim > 1) v[1] = (float)j / (float)(count - 1);
					if (dim > 2) v[2] = b.randoms[s];
				}
			}
			break;
		default:
			std::memset(out, 0, sizeof(float) * (size_t)n * (size_t)dim);
			break;
		}
		vb->commit(out);
		m_vertexBuffers.push_back(std::move(vb));
	}

	// Strands in the order of their random: a prefix of any length is an even sample of the groom.
	m_strandOrder.resize(b.strandCount());
	std::iota(m_strandOrder.begin(), m_strandOrder.end(), 0u);
	std::stable_sort(m_strandOrder.begin(), m_strandOrder.end(),
	                 [&](uint32_t x, uint32_t y) { return b.randoms[x] < b.randoms[y]; });
}

std::unique_ptr<MIndexBuffer> HairDrawOverride::buildIndices(const HairBuffers& b, float percent) const {
	const uint32_t strands = b.strandCount();
	const uint32_t shown = (uint32_t)std::lround((double)strands * std::clamp(percent, 0.0f, 100.0f) / 100.0);
	uint64_t segments = 0;
	for (uint32_t i = 0; i < shown; i++) segments += b.pointCounts[m_strandOrder[i]] - 1;
	auto buffer = std::make_unique<MIndexBuffer>(MGeometry::kUnsignedInt32);
	if (segments == 0) return buffer;
	auto* out = static_cast<uint32_t*>(buffer->acquire((unsigned int)(segments * 2), true));
	if (!out) return buffer;
	size_t w = 0;
	for (uint32_t i = 0; i < shown; i++) {
		const uint32_t s = m_strandOrder[i];
		const uint32_t first = b.offsets[s];
		for (uint32_t j = 0; j + 1 < b.pointCounts[s]; j++) {
			out[w++] = first + j;
			out[w++] = first + j + 1;
		}
	}
	buffer->commit(out);
	return buffer;
}

void HairDrawOverride::rebuildIndices(const HairBuffers& b, float percent) {
	m_indexBuffer = buildIndices(b, percent);
	m_wireIndexBuffer = buildIndices(b, std::min(percent, kWirePercent));
}

void HairDrawOverride::updateWireframe(MSubSceneContainer& container, const State& s, bool geometryChanged) {
	MRenderItem* wire = container.find(kWireItemName);
	const DisplayStatus status = MGeometryUtilities::displayStatus(m_paths[0]);
	const bool selected = status == kLead || status == kActive || status == kHilite || status == kActiveComponent;
	if (!wire) {
		MRenderer* renderer = MRenderer::theRenderer();
		const MShaderManager* sm = renderer ? renderer->getShaderManager() : nullptr;
		if (!sm) return;
		if (!m_wireShader) m_wireShader = sm->getStockShader(MShaderManager::k3dSolidShader);
		if (!m_wireShader) return;
		wire = MRenderItem::Create(kWireItemName, MRenderItem::DecorationItem, MGeometry::kLines);
		wire->setDrawMode(MGeometry::kAll);
		wire->depthPriority(MRenderItem::sActiveWireDepthPriority);
		wire->castsShadows(false);
		wire->receivesShadows(false);
		wire->setShader(m_wireShader);
		container.add(wire);
	}
	if (geometryChanged) m_wireGeometryCurrent = false;
	wire->enable(selected);
	if (!selected) return;
	if (!m_wireGeometryCurrent && m_wireIndexBuffer) {
		// The strands' own positions: the same vertex buffer the hair draws from.
		MVertexBufferArray array;
		for (const auto& vb : m_vertexBuffers) {
			if (vb->descriptor().semantic() == MGeometry::kPosition) array.addBuffer(vb->descriptor().semanticName(), vb.get());
		}
		const MBoundingBox box = m_shape->boundingBox();
		setGeometryForRenderItem(*wire, array, *m_wireIndexBuffer, &box);
		m_wireGeometryCurrent = true;
	}
	// Maya's own selection colour for this state (lead / active), as a mesh's wireframe gets.
	const MColor c = MGeometryUtilities::wireframeColor(m_paths[0]);
	const float color[4] = { c.r, c.g, c.b, 1.0f };
	m_wireShader->setParameter("solidColor", color);
	if (m_paths.length() == 1) {
		const MMatrix m = m_paths[0].inclusiveMatrix();
		wire->setMatrix(&m);
	} else {
		MMatrixArray matrices;
		for (unsigned int i = 0; i < m_paths.length(); i++) matrices.append(m_paths[i].inclusiveMatrix());
		setInstanceTransformArray(*wire, matrices);
	}
}

void HairDrawOverride::update(MSubSceneContainer& container, const MFrameContext&) {
	const State s = currentState();
	// Only the instances that are visible are drawn: hiding the shape, its transform, a parent or
	// its display layer hides the hair, as it does a mesh.
	MDagPathArray allPaths;
	MDagPath::getAllPathsTo(m_node, allPaths);
	m_paths.clear();
	for (unsigned int i = 0; i < allPaths.length(); i++) {
		if (allPaths[i].isVisible()) m_paths.append(allPaths[i]);
	}
	MRenderItem* item = container.find(kItemName);
	auto buffers = m_shape ? m_shape->buffers() : nullptr;
	if (!buffers || buffers->pointCount() == 0 || m_paths.length() == 0 || !ensureShader()) {
		if (item) item->enable(false);
		if (MRenderItem* wire = container.find(kWireItemName)) wire->enable(false);
		m_state = s;
		m_haveGeometry = true;
		return;
	}

	if (!item) {
		item = MRenderItem::Create(kItemName, MRenderItem::MaterialSceneItem, MGeometry::kLines);
		item->setDrawMode(MGeometry::kAll);
		item->castsShadows(false);
		item->receivesShadows(false);
		// Pickable whenever polygons are (the default selection filter).
		item->setSelectionMask(MSelectionMask(MSelectionMask::kSelectMeshes));
		// In the depth/normal prepass that viewport effects (SSAO, depth of field) read.
		item->setExcludedFromPostEffects(false);
		item->setShader(m_shader);
		container.add(item);
	}
	item->enable(true);

	const bool dataChanged = buffers->revision != m_state.dataRevision || m_vertexBuffers.empty();
	if (dataChanged) rebuildBuffers(*buffers);
	if (dataChanged || s.displayPercent != m_state.displayPercent || !m_indexBuffer) rebuildIndices(*buffers, s.displayPercent);

	const bool geometryChanged = dataChanged || s.displayPercent != m_state.displayPercent;
	if (geometryChanged) {
		MVertexBufferArray array;
		for (const auto& vb : m_vertexBuffers) array.addBuffer(vb->descriptor().semanticName(), vb.get());
		const MBoundingBox box = m_shape->boundingBox();
		setGeometryForRenderItem(*item, array, *m_indexBuffer, &box);
	}

	if (m_customShader) {
		m_shader->setParameter("gWidthScale", plugValue<float>(m_node, HairShape::aWidthScale, 1.0f));
		m_shader->setParameter("gUseSetColor", s.flatColor ? 1.0f : 0.0f);
		const float flat[4] = { s.color[0], s.color[1], s.color[2], 1.0f };
		m_shader->setParameter("gSetColor", flat);
		m_shader->setParameter("gUseGradient", s.gradient ? 1.0f : 0.0f);
		m_shader->setArrayParameter("gGradient", s.gradientColors, 16 * 3);
	}

	if (m_paths.length() == 1) {
		const MMatrix m = m_paths[0].inclusiveMatrix();
		item->setMatrix(&m);
	} else {
		MMatrixArray matrices;
		for (unsigned int i = 0; i < m_paths.length(); i++) matrices.append(m_paths[i].inclusiveMatrix());
		setInstanceTransformArray(*item, matrices);
	}

	updateWireframe(container, s, geometryChanged);

	m_state = s;
	m_state.dataRevision = buffers->revision;
	m_haveGeometry = true;
}

bool HairDrawOverride::getSelectionPath(const MRenderItem&, MDagPath& dagPath) const {
	// Without this a pick on the strands resolves to no object at all.
	if (m_paths.length() == 0) return false;
	dagPath = m_paths[0];
	return true;
}

bool HairDrawOverride::getInstancedSelectionPath(const MRenderItem&, const MIntersection& intersection, MDagPath& dagPath) const {
	if (m_paths.length() == 0) return false;
	const int instance = intersection.instanceID();
	dagPath = m_paths[(instance >= 1 && (unsigned int)instance <= m_paths.length()) ? (unsigned int)(instance - 1) : 0u];
	return true;
}

} // namespace SisirMaya
