#include "HairRenderItem.h"

#include "HairObject.h"
#include "MaxUtil.h"

#include <bitmap.h>
#include <Graphics/DrawContext.h>
#include <Graphics/IViewportViewSetting.h>
#include <Graphics/IVirtualDevice.h>
#include <imtl.h>
#include <TextureMapIndexConstants.h>
#include <inode.h>

#include <QDir>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace SisirMax {

using namespace MaxSDK::Graphics;
using SisirDcc::kGradientSamples;

namespace {

// How much of the groom the selection wireframe draws: enough to read the shape, not a solid block.
constexpr float kWirePercent = 20.0f;

QString g_shaderDir;
HLSLMaterialHandle g_hairShader;
HLSLMaterialHandle g_wireShader;
bool g_shadersTried = false;
bool g_shadersOk = false;
MaterialRequiredStreams g_hairFormat;
MaterialRequiredStreams g_wireFormat;

void addElement(MaterialRequiredStreams& format, MeshChannelCategory category, VertexFieldType type, unsigned char stream) {
	MaterialRequiredStreamElement e;
	e.SetChannelCategory(category);
	e.SetType(type);
	e.SetUsageIndex(0);
	e.SetStreamIndex(stream);
	e.SetOffset(0);
	format.AddStream(e);
}

// A material parameter by its internal name, in any of its parameter blocks.
bool findParam(Animatable* a, const wchar_t* name, IParamBlock2*& pb, ParamID& id) {
	if (!a) return false;
	for (int b = 0; b < a->NumParamBlocks(); b++) {
		IParamBlock2* block = a->GetParamBlock(b);
		if (!block) continue;
		for (int i = 0; i < block->NumParams(); i++) {
			const ParamID pid = block->IndextoID(i);
			const ParamDef& def = block->GetParamDef(pid);
			if (def.int_name && _wcsicmp(def.int_name, name) == 0) {
				pb = block;
				id = pid;
				return true;
			}
		}
	}
	return false;
}

bool paramColor(Animatable* a, const wchar_t* name, float out[3]) {
	IParamBlock2* pb;
	ParamID id;
	if (!findParam(a, name, pb, id)) return false;
	const ParamType2 type = (ParamType2)(pb->GetParamDef(id).type & ~TYPE_TAB);
	if (type != TYPE_RGBA && type != TYPE_FRGBA && type != TYPE_POINT3) return false;
	const AColor c = type == TYPE_FRGBA ? pb->GetAColor(id) : AColor(pb->GetColor(id));
	out[0] = c.r;
	out[1] = c.g;
	out[2] = c.b;
	return true;
}

bool paramFloat(Animatable* a, const wchar_t* name, float& out) {
	IParamBlock2* pb;
	ParamID id;
	if (!findParam(a, name, pb, id)) return false;
	const ParamType2 type = (ParamType2)(pb->GetParamDef(id).type & ~TYPE_TAB);
	if (type != TYPE_FLOAT && type != TYPE_PCNT_FRAC && type != TYPE_WORLD && type != TYPE_ANGLE) return false;
	out = pb->GetFloat(id);
	return true;
}

Texmap* paramMap(Animatable* a, const wchar_t* name) {
	IParamBlock2* pb;
	ParamID id;
	if (!findParam(a, name, pb, id)) return nullptr;
	if ((pb->GetParamDef(id).type & ~TYPE_TAB) != TYPE_TEXMAP) return nullptr;
	return pb->GetTexmap(id);
}

// The importer's hair material reads Sisir's per-point colour through a user-data map whose
// attribute is "sisir_albedo" (SceneBuilder.cpp).
bool isSisirAlbedo(Texmap* map) {
	IParamBlock2* pb;
	ParamID id;
	if (!map || !findParam(map, L"attribute", pb, id)) return false;
	if ((pb->GetParamDef(id).type & ~TYPE_TAB) != TYPE_STRING) return false;
	const MCHAR* s = pb->GetStr(id);
	return s && _wcsicmp(s, L"sisir_albedo") == 0;
}

} // namespace

void HairRenderItem::setShaderDirectory(const QString& dir) { g_shaderDir = dir; }

bool HairRenderItem::ensureShaders() {
	if (g_shadersTried) return g_shadersOk;
	g_shadersTried = true;
	const bool hair = g_hairShader.InitializeWithFile(toM(QDir(g_shaderDir).filePath("SisirHair.fx")));
	const bool wire = g_wireShader.InitializeWithFile(toM(QDir(g_shaderDir).filePath("SisirHairWire.fx")));
	g_shadersOk = hair && wire;
	if (!g_shadersOk) logWarning("the hair viewport shaders did not load from " + g_shaderDir + "; hair is not drawn in the viewport");
	addElement(g_hairFormat, MeshChannelPosition, VertexFieldFloat3, 0);
	addElement(g_hairFormat, MeshChannelVertexColor, VertexFieldFloat4, 1);
	addElement(g_hairFormat, MeshChannelTangent, VertexFieldFloat3, 2);
	addElement(g_hairFormat, MeshChannelTexcoord, VertexFieldFloat3, 3);
	addElement(g_wireFormat, MeshChannelPosition, VertexFieldFloat3, 0);
	return g_shadersOk;
}

HairRenderItem::HairRenderItem(HairObject& hair) : m_hair(&hair) {}

HairRenderItem::~HairRenderItem() = default;

void HairRenderItem::rebuildBuffers(const SisirDcc::HairBuffers& b) {
	const size_t n = b.pointCount();
	m_streams.removeAll();
	m_wireStreams.removeAll();
	if (n == 0) return;

	std::vector<float> data(n * 4);
	// Positions: as stored.
	VertexBufferHandle positions;
	positions.Initialize(sizeof(float) * 3, n, const_cast<float*>(b.positions.data()));
	// Colours: Sisir's albedo, linear (the space Max's viewport shades in).
	float lut[256];
	for (int c = 0; c < 256; c++) lut[c] = SisirDcc::srgbToLinear((uint8_t)c);
	for (size_t i = 0; i < n; i++) {
		data[i * 4] = lut[b.albedo[i * 4]];
		data[i * 4 + 1] = lut[b.albedo[i * 4 + 1]];
		data[i * 4 + 2] = lut[b.albedo[i * 4 + 2]];
		data[i * 4 + 3] = 1.0f;
	}
	VertexBufferHandle colors;
	colors.Initialize(sizeof(float) * 4, n, data.data());
	// Tangents: central differences along each strand.
	data.assign(n * 3, 0.0f);
	for (uint32_t s = 0; s < b.strandCount(); s++) {
		const uint32_t first = b.offsets[s], count = b.pointCounts[s];
		for (uint32_t j = 0; j < count; j++) {
			const uint32_t a = first + (j > 0 ? j - 1 : 0), c = first + std::min(j + 1, count - 1);
			float t[3];
			for (int k = 0; k < 3; k++) t[k] = b.positions[(size_t)c * 3 + k] - b.positions[(size_t)a * 3 + k];
			const float len = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
			for (int k = 0; k < 3; k++) data[(size_t)(first + j) * 3 + k] = len > 0.0f ? t[k] / len : 0.0f;
		}
	}
	VertexBufferHandle tangents;
	tangents.Initialize(sizeof(float) * 3, n, data.data());
	// x: the strand's width there; y: how far along it, root 0 to tip 1; z: the strand's random.
	for (uint32_t s = 0; s < b.strandCount(); s++) {
		const uint32_t first = b.offsets[s], count = b.pointCounts[s];
		for (uint32_t j = 0; j < count; j++) {
			float* v = data.data() + (size_t)(first + j) * 3;
			v[0] = b.widths[first + j];
			v[1] = (float)j / (float)(count - 1);
			v[2] = b.randoms[s];
		}
	}
	VertexBufferHandle widthAlong;
	widthAlong.Initialize(sizeof(float) * 3, n, data.data());

	m_streams.append(positions);
	m_streams.append(colors);
	m_streams.append(tangents);
	m_streams.append(widthAlong);
	m_wireStreams.append(positions);

	// Strands in the order of their random: a prefix of any length is an even sample of the groom.
	m_strandOrder.resize(b.strandCount());
	std::iota(m_strandOrder.begin(), m_strandOrder.end(), 0u);
	std::stable_sort(m_strandOrder.begin(), m_strandOrder.end(),
	                 [&](uint32_t x, uint32_t y) { return b.randoms[x] < b.randoms[y]; });
}

IndexBufferHandle HairRenderItem::buildIndices(const SisirDcc::HairBuffers& b, float percent, size_t& segments) const {
	const uint32_t strands = b.strandCount();
	const uint32_t shown = (uint32_t)std::lround((double)strands * std::clamp(percent, 0.0f, 100.0f) / 100.0);
	segments = 0;
	for (uint32_t i = 0; i < shown; i++) segments += b.pointCounts[m_strandOrder[i]] - 1;
	IndexBufferHandle buffer;
	if (segments == 0) return buffer;
	std::vector<uint32_t> indices(segments * 2);
	size_t w = 0;
	for (uint32_t i = 0; i < shown; i++) {
		const uint32_t s = m_strandOrder[i];
		const uint32_t first = b.offsets[s];
		for (uint32_t j = 0; j + 1 < b.pointCounts[s]; j++) {
			indices[w++] = first + j;
			indices[w++] = first + j + 1;
		}
	}
	buffer.Initialize(IndexTypeInt, indices.size(), indices.data());
	return buffer;
}

void HairRenderItem::Realize(DrawContext&) {
	if (!m_hair || !ensureShaders()) return;
	auto b = m_hair->buffers();
	if (!b || b->pointCount() == 0) {
		m_segments = m_wireSegments = 0;
		return;
	}
	const bool dataChanged = b->revision != m_dataRevision || m_streams.length() == 0;
	if (dataChanged) {
		rebuildBuffers(*b);
		m_dataRevision = b->revision;
	}
	const float percent = m_hair->paramFloat(kDisplayPercent);
	if (dataChanged || percent != m_displayPercent) {
		m_indices = buildIndices(*b, percent, m_segments);
		m_wireIndices = buildIndices(*b, std::min(percent, kWirePercent), m_wireSegments);
		m_displayPercent = percent;
	}
}

bool HairRenderItem::sampleAlongStrand(Texmap* map, float out[kGradientSamples * 3]) {
	const unsigned long now = GetTickCount();
	if (map == m_sampledMap && now - m_sampledAt < 1000) {
		std::memcpy(out, m_sampled, sizeof(m_sampled));
		return m_sampleOk;
	}
	m_sampledMap = map;
	m_sampledAt = now;
	m_sampleOk = false;
	// Max renders the map over its UV square: u 0.5, v from 0 (root) to 1 (tip) -- the coordinates
	// Arnold gives a curve, so a ramp reads root to tip in both.
	BitmapInfo bi;
	bi.SetWidth(1);
	bi.SetHeight(kGradientSamples);
	bi.SetType(BMM_TRUE_64);
	Bitmap* bm = TheManager->Create(&bi);
	if (!bm) return false;
	map->RenderBitmap(GetCOREInterface()->GetTime(), bm, 1.0f, FALSE);
	float peak = 0.0f;
	for (int i = 0; i < kGradientSamples; i++) {
		BMM_Color_64 px;
		bm->GetPixels(0, kGradientSamples - 1 - i, 1, &px); // row 0 is the top: v = 1
		const uint16_t c[3] = { px.r, px.g, px.b };
		for (int k = 0; k < 3; k++) {
			m_sampled[i * 3 + k] = SisirDcc::srgbToLinear((uint8_t)(c[k] >> 8));
			peak = std::max(peak, m_sampled[i * 3 + k]);
		}
	}
	bm->DeleteThis();
	// All black is what Max returns for maps it cannot evaluate (Arnold's own maps): not a colour,
	// so the caller falls back to the neutral tint.
	m_sampleOk = peak > 1.0e-4f;
	std::memcpy(out, m_sampled, sizeof(m_sampled));
	return m_sampleOk;
}

HairRenderItem::ViewColor HairRenderItem::viewColor(INode* node, int mode) {
	ViewColor vc;
	Mtl* mtl = node ? node->GetMtl() : nullptr;
	if (!mtl) return vc;
	float rgb[3] = { 0.8f, 0.8f, 0.8f };
	Texmap* map = paramMap(mtl, L"base_color_shader");
	if (!map) map = paramMap(mtl, L"base_color_map");
	if (!map) map = mtl->GetSubTexmap(ID_DI);
	if (isSisirAlbedo(map)) return vc;
	vc.perPoint = false;
	if (map) {
		// Driven by a map: a neutral value here (the map itself is sampled along the strand), never the
		// slot's own colour. White when the material has melanin, so the melanin colour shows as the
		// render has it; mid-grey for a plain surface colour.
		float melanin;
		rgb[0] = rgb[1] = rgb[2] = paramFloat(mtl, L"melanin", melanin) ? 1.0f : 0.5f;
		if (sampleAlongStrand(map, vc.gradientColors)) {
			vc.gradient = true;
			rgb[0] = rgb[1] = rgb[2] = 1.0f; // the samples carry the colour; melanin and weight tint them
		}
	} else if (!paramColor(mtl, L"base_color", rgb)) {
		const Color d = mtl->GetDiffuse();
		rgb[0] = d.r;
		rgb[1] = d.g;
		rgb[2] = d.b;
	}
	float weight = 1.0f;
	paramFloat(mtl, L"base", weight);
	float melanin;
	if (paramFloat(mtl, L"melanin", melanin)) {
		float redness = 0.5f;
		paramFloat(mtl, L"melanin_redness", redness);
		float tint[3];
		SisirDcc::arnoldMelaninColor(melanin, redness, tint);
		for (int k = 0; k < 3; k++) rgb[k] *= tint[k];
	}
	for (int k = 0; k < 3; k++) vc.rgb[k] = std::clamp(rgb[k] * weight, 0.0f, 1.0f);
	(void)mode;
	return vc;
}

void HairRenderItem::Display(DrawContext& drawContext) {
	if (!m_hair || !g_shadersOk || m_segments == 0) return;
	const IViewportViewSetting* settings = drawContext.GetViewportSettings();
	if (settings && settings->GetViewportVisualStyle() == VisualStyleBoundingBox) return;
	// One draw per view and frame, whichever of the node's two items (shaded, wireframe) asks.
	const int frame = drawContext.GetFrameStamp();
	const void* view = drawContext.GetViewExp();
	if (!drawContext.IsHitTest()) {
		if (frame == m_lastFrame && view == m_lastView) return;
		m_lastFrame = frame;
		m_lastView = view;
	}

	INode* node = drawContext.GetCurrentNode();
	const int mode = m_hair->paramInt(kColorMode);
	bool flat = false, gradient = false;
	float color[3] = { 0.8f, 0.8f, 0.8f };
	float gradientColors[kGradientSamples * 3] = {};
	if (mode == 2) {
		const Point3 c = m_hair->paramColor(kSetColor);
		flat = true;
		color[0] = c.x;
		color[1] = c.y;
		color[2] = c.z;
	} else if (mode == 0 || mode == 3) {
		const ViewColor vc = viewColor(node, mode);
		flat = !vc.perPoint || mode == 3;
		std::memcpy(color, vc.rgb, sizeof(color));
		gradient = vc.gradient;
		std::memcpy(gradientColors, vc.gradientColors, sizeof(gradientColors));
	}

	IVirtualDevice& vd = drawContext.GetVirtualDevice();
	g_hairShader.SetFloatParameter(L"gWidthScale", m_hair->paramFloat(kWidthScale));
	g_hairShader.SetFloatParameter(L"gUseSetColor", flat ? 1.0f : 0.0f);
	g_hairShader.SetFloat4Parameter(L"gSetColor", Point4(color[0], color[1], color[2], 1.0f));
	g_hairShader.SetFloatParameter(L"gUseGradient", gradient ? 1.0f : 0.0f);
	g_hairShader.SetFloatArrayParameter(L"gGradient", gradientColors, kGradientSamples * 3);
	g_hairShader.SetFloat4Parameter(L"gTint", Point4(1.0f, 1.0f, 1.0f, 0.0f));
	vd.SetStreamFormat(g_hairFormat);
	vd.SetVertexStreams(m_streams);
	vd.SetIndexBuffer(m_indices);
	g_hairShader.Activate(drawContext);
	const unsigned int passes = g_hairShader.GetPassCount(drawContext);
	for (unsigned int p = 0; p < passes; p++) {
		g_hairShader.ActivatePass(drawContext, p);
		vd.Draw(PrimitiveLineList, 0, (int)m_segments);
	}
	g_hairShader.PassesFinished(drawContext);
	g_hairShader.Terminate();

	// Max's selection colour over a thinned set of strands, as a mesh's selection wireframe shows.
	if (node && node->Selected() && m_wireSegments > 0 && !drawContext.IsHitTest()) {
		const Point3 sel = GetUIColor(COLOR_SELECTION);
		g_wireShader.SetFloat4Parameter(L"gColor", Point4(sel.x, sel.y, sel.z, 1.0f));
		vd.SetStreamFormat(g_wireFormat);
		vd.SetVertexStreams(m_wireStreams);
		vd.SetIndexBuffer(m_wireIndices);
		g_wireShader.Activate(drawContext);
		const unsigned int wirePasses = g_wireShader.GetPassCount(drawContext);
		for (unsigned int p = 0; p < wirePasses; p++) {
			g_wireShader.ActivatePass(drawContext, p);
			vd.Draw(PrimitiveLineList, 0, (int)m_wireSegments);
		}
		g_wireShader.PassesFinished(drawContext);
		g_wireShader.Terminate();
	}
}

} // namespace SisirMax
