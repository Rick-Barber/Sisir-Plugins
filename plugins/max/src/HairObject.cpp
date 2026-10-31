#include "HairObject.h"

#include "ClassIds.h"
#include "HairRenderItem.h"
#include "HairRollout.h"
#include "HairTranslator.h"
#include "MaxUtil.h"
#include "RenderingSpace.h"

#include <Graphics/CustomRenderItemHandle.h>
#include <Graphics/IRenderItemContainer.h>
#include <Graphics/RenderNodeHandle.h>
#include <Graphics/UpdateNodeContext.h>
#include <gfx.h>
#include <inode.h>
#include <mesh.h>

#include <algorithm>
#include <numeric>

namespace SisirMax {

using namespace MaxSDK::Graphics;

namespace {

enum { kParamsBlock = 0 };
enum { kParamsRef = 0 };

// How many strands click selection tests: an even sample is enough to hit, and fast.
constexpr uint32_t kPickStrands = 20000;

class HairObjectClassDesc : public ClassDesc2 {
public:
	// Created by the importer, never from the Create panel.
	int IsPublic() override { return FALSE; }
	void* Create(BOOL) override { return new HairObject(); }
	const MCHAR* ClassName() override { return _M("Sisir Hair"); }
	const MCHAR* NonLocalizedClassName() override { return _M("Sisir Hair"); }
	SClass_ID SuperClassID() override { return GEOMOBJECT_CLASS_ID; }
	Class_ID ClassID() override { return kHairObjectClassId; }
	const MCHAR* Category() override { return _M("Sisir"); }
	const MCHAR* InternalName() override { return _M("SisirHair"); }
	HINSTANCE HInstance() override { return SisirMax::hInstance(); }
	MaxSDK::QMaxParamBlockWidget* CreateQtWidget(ReferenceMaker&, IParamBlock2& block, const MapID, MSTR& title,
	                                              int&, int&) override {
		title = _M("Sisir Hair");
		return new HairRollout(block);
	}
};

HairObjectClassDesc g_desc;

// The same names and defaults as sisirHairShape's attributes (plugins/maya/src/HairShape.cpp).
ParamBlockDesc2 g_params(kParamsBlock, _M("params"), 0, &g_desc, P_AUTO_CONSTRUCT + P_AUTO_UI_QT, kParamsRef,
	kWidthScale, _M("widthScale"), TYPE_FLOAT, P_ANIMATABLE, 0,
		p_default, 1.0f, p_range, 0.0f, 1000.0f, p_end,
	kDisplayPercent, _M("displayPercent"), TYPE_FLOAT, 0, 0,
		p_default, 100.0f, p_range, 0.0f, 100.0f, p_end,
	kRenderPercent, _M("renderPercent"), TYPE_FLOAT, 0, 0,
		p_default, 100.0f, p_range, 0.0f, 100.0f, p_end,
	kColorMode, _M("colorMode"), TYPE_INT, 0, 0,
		p_default, 0, p_range, 0, 3, p_end,
	kSetColor, _M("setColor"), TYPE_RGBA, P_ANIMATABLE, 0,
		p_default, Color(0.9f, 0.8f, 0.65f), p_end,
	kMaterialId, _M("sisirMaterialId"), TYPE_INT, P_INVISIBLE, 0,
		p_default, 0, p_range, 0, 9, p_end,
	kGroomRoughness, _M("sisirGroomRoughness"), TYPE_FLOAT, P_INVISIBLE, 0,
		p_default, 0.05f, p_range, 0.0f, 1.0f, p_end,
	kGroomAo, _M("sisirGroomAo"), TYPE_FLOAT, P_INVISIBLE, 0,
		p_default, 1.0f, p_range, 0.0f, 1.0f, p_end,
	kSourceFile, _M("sisirSourceFile"), TYPE_FILENAME, 0, 0,
		p_default, _M(""), p_end,
	kHairSetName, _M("sisirHairSet"), TYPE_STRING, 0, 0,
		p_default, _M(""), p_end,
	p_end);

Mesh g_emptyMesh;

} // namespace

ClassDesc2* HairObject::classDesc() { return &g_desc; }

HairObject::HairObject() : m_translator(std::make_unique<HairTranslator>(*this)) { g_desc.MakeAutoParamBlocks(this); }

HairObject::~HairObject() {
	// Nitrous may hold a render item a little longer than the object lives.
	for (auto& entry : m_renderItems) {
		static_cast<HairRenderItem*>(entry.second)->detach();
		entry.second->Release();
	}
	DeleteAllRefs();
}

void HairObject::setBuffers(std::shared_ptr<const HairBuffers> buffers) {
	m_buffers = std::move(buffers);
	NotifyDependents(FOREVER, PART_GEOM | PART_DISPLAY, REFMSG_CHANGE);
}

float HairObject::paramFloat(ParamID id) const { return m_pblock ? m_pblock->GetFloat(id) : 0.0f; }
int HairObject::paramInt(ParamID id) const { return m_pblock ? m_pblock->GetInt(id) : 0; }
Point3 HairObject::paramColor(ParamID id) const {
	if (!m_pblock) return Point3(0.8f, 0.8f, 0.8f);
	const Color c = m_pblock->GetColor(id);
	return Point3(c.r, c.g, c.b);
}
QString HairObject::paramString(ParamID id) const { return m_pblock ? toQ(m_pblock->GetStr(id)) : QString(); }

Class_ID HairObject::ClassID() { return kHairObjectClassId; }

void HairObject::GetClassName(MSTR& s, bool) const { s = _M("Sisir Hair"); }

const MCHAR* HairObject::GetObjectName(bool) const { return _M("Sisir Hair"); }

MSTR HairObject::SubAnimName(int i, bool) { return i == 0 ? MSTR(_M("Parameters")) : MSTR(); }

IParamBlock2* HairObject::GetParamBlockByID(BlockID id) { return m_pblock && m_pblock->ID() == id ? m_pblock : nullptr; }

void HairObject::BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) { g_desc.BeginEditParams(ip, this, flags, prev); }

void HairObject::EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) { g_desc.EndEditParams(ip, this, flags, next); }

BaseInterface* HairObject::GetInterface(Interface_ID id) {
	if (id == MAXTOA_TranslationInterface_ID) return m_translator.get();
	return GeomObject::GetInterface(id);
}

void HairObject::SetReference(int i, RefTargetHandle rtarg) {
	if (i == kParamsRef) m_pblock = static_cast<IParamBlock2*>(rtarg);
}

RefResult HairObject::NotifyRefChanged(const Interval&, RefTargetHandle, PartID&, RefMessage, BOOL) { return REF_SUCCEED; }

RefTargetHandle HairObject::Clone(RemapDir& remap) {
	auto* copy = new HairObject();
	copy->ReplaceReference(kParamsRef, remap.CloneRef(m_pblock));
	copy->m_buffers = m_buffers; // immutable: shared, never copied
	BaseClone(this, copy, remap);
	return copy;
}

IOResult HairObject::Save(ISave* isave) {
	if (!m_buffers) return IO_OK;
	const std::vector<char> blob = SisirDcc::serializeHair(*m_buffers);
	isave->BeginChunk(kHairDataChunk);
	ULONG written = 0;
	const IOResult r = isave->WriteVoid(blob.data(), (ULONG)blob.size(), &written);
	isave->EndChunk();
	return r == IO_OK && written == blob.size() ? IO_OK : IO_ERROR;
}

IOResult HairObject::Load(ILoad* iload) {
	IOResult r;
	while ((r = iload->OpenChunk()) == IO_OK) {
		if (iload->CurChunkID() == kHairDataChunk) {
			const ULONGLONG length = iload->CurChunkLength();
			std::vector<char> blob((size_t)length);
			ULONG read = 0;
			if (length && iload->ReadVoid(blob.data(), (ULONG)length, &read) != IO_OK) return IO_ERROR;
			m_buffers = SisirDcc::deserializeHair(blob.data(), read);
			if (!m_buffers) logWarning("a Sisir Hair object's strands could not be read from the scene file");
		}
		iload->CloseChunk();
	}
	return r == IO_END ? IO_OK : r;
}

Box3 HairObject::localBounds() const {
	if (!m_buffers || m_buffers->pointCount() == 0) return Box3(Point3(-0.5f, -0.5f, -0.5f), Point3(0.5f, 0.5f, 0.5f));
	const float pad = 0.5f * m_buffers->maxWidth * std::max(paramFloat(kWidthScale), 0.0f);
	return Box3(Point3(m_buffers->boundsMin[0] - pad, m_buffers->boundsMin[1] - pad, m_buffers->boundsMin[2] - pad),
	            Point3(m_buffers->boundsMax[0] + pad, m_buffers->boundsMax[1] + pad, m_buffers->boundsMax[2] + pad));
}

void HairObject::GetLocalBoundBox(TimeValue, INode*, ViewExp*, Box3& box) { box = localBounds(); }

void HairObject::GetWorldBoundBox(TimeValue t, INode* inode, ViewExp*, Box3& box) {
	box = localBounds();
	if (inode) box = box * inode->GetObjectTM(t);
}

void HairObject::GetDeformBBox(TimeValue, Box3& box, Matrix3* tm, BOOL) {
	box = localBounds();
	if (tm) box = box * (*tm);
}

Mesh* HairObject::GetRenderMesh(TimeValue, INode*, View&, BOOL& needDelete) {
	needDelete = FALSE;
	return &g_emptyMesh;
}

int HairObject::HitTest(TimeValue t, INode* inode, int type, int crossing, int flags, IPoint2* p, ViewExp* vpt) {
	if (!vpt || !vpt->IsAlive() || !m_buffers || m_buffers->strandCount() == 0) return 0;
	const HairBuffers& b = *m_buffers;
	GraphicsWindow* gw = vpt->getGW();
	HitRegion region;
	MakeHitRegion(region, type, crossing, 4, p);
	const DWORD limits = gw->getRndLimits();
	gw->setRndLimits((limits | GW_PICK) & ~GW_ILLUM);
	gw->setHitRegion(&region);
	gw->setTransform(inode->GetObjectTM(t));
	gw->clearHitCode();
	// An even sample of the strands (every k-th), each as a polyline.
	const uint32_t strands = b.strandCount();
	const uint32_t step = std::max(1u, strands / kPickStrands);
	std::vector<Point3> line;
	int hit = 0;
	for (uint32_t s = 0; s < strands && !hit; s += step) {
		const uint32_t first = b.offsets[s], count = b.pointCounts[s];
		line.resize(count);
		for (uint32_t j = 0; j < count; j++) {
			const float* q = b.positions.data() + (size_t)(first + j) * 3;
			line[j] = Point3(q[0], q[1], q[2]);
		}
		gw->polyline((int)count, line.data(), nullptr, nullptr, FALSE, nullptr);
		// A window selection needs the whole object inside: every sampled strand.
		if (gw->checkHitCode() && (crossing || type == HITTYPE_POINT)) hit = 1;
	}
	if (!hit && !(crossing || type == HITTYPE_POINT)) hit = gw->checkHitCode() ? 1 : 0;
	gw->setRndLimits(limits);
	(void)flags;
	return hit;
}

bool HairObject::PrepareDisplay(const UpdateDisplayContext&) { return true; }

bool HairObject::UpdatePerNodeItems(const UpdateDisplayContext&, UpdateNodeContext& nodeContext,
                                    IRenderItemContainer& targetRenderItemContainer) {
	INode* node = nodeContext.GetRenderNode().GetMaxNode();
	ICustomRenderItem*& item = m_renderItems[node];
	if (!item) {
		item = new HairRenderItem(*this);
		item->AddRef();
	}
	// One drawing, in shaded and in wireframe styles alike (the item skips a second draw per frame).
	for (const RenderItemVisibilityGroup group : { RenderItemVisible_Shaded, RenderItemVisible_Wireframe }) {
		CustomRenderItemHandle handle;
		handle.Initialize();
		handle.SetCustomImplementation(item);
		handle.SetVisibilityGroup(group);
		targetRenderItemContainer.AddRenderItem(handle);
	}
	return true;
}

bool hairExportView(HairObject& hair, SisirHairExportView& out) {
	auto b = hair.buffers();
	if (!b || b->strandCount() == 0) return false;
	// The view's pointers outlive this call only while the object keeps the same buffers.
	static thread_local std::shared_ptr<const HairBuffers> keepAlive;
	keepAlive = b;
	out = SisirHairExportView();
	out.strandCount = b->strandCount();
	out.pointCount = b->pointCount();
	out.pointCounts = b->pointCounts.data();
	out.positions = b->positions.data();
	out.widths = b->widths.data();
	out.albedo = b->albedo.data();
	out.rootUvs = b->rootUvs.data();
	out.clumpIds = b->clumpIds.data();
	out.randoms = b->randoms.data();
	out.widthScale = hair.paramFloat(kWidthScale);
	out.renderPercent = hair.paramFloat(kRenderPercent);
	renderingSpaceMatrix(out.toRenderingSpace);
	return true;
}

} // namespace SisirMax

extern "C" __declspec(dllexport) bool sisirGetHairExportView(Object* object, SisirHairExportView* out) {
	if (!object || !out || object->ClassID() != SisirMax::kHairObjectClassId) return false;
	return SisirMax::hairExportView(*static_cast<SisirMax::HairObject*>(object), *out);
}
