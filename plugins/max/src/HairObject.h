#pragma once

// Sisir Hair: one hair set as a 3ds Max geometry object -- what Maya's sisirHairShape is there.
//
// The strands live in an immutable, shared HairBuffers (plugins/common): clones and undo copies
// share one array, and the .max file stores it as one compressed chunk. The parameters (width,
// display / render percent, colour mode, set colour, Sisir's material id...) are a ParamBlock2 with
// the same names and defaults as the Maya attributes. The viewport draws camera-facing ribbons
// through a Nitrous render item (HairRenderItem); Arnold renders the hair through MAXtoA's
// translation interface (HairTranslator.cpp). Nothing converts it to a mesh.

#include "HairBuffers.h"
#include "HairExportView.h"

#include <iparamb2.h>
#include <object.h>

#include <map>
#include <memory>

namespace MaxSDK { namespace Graphics { class ICustomRenderItem; } }

namespace SisirMax {

using SisirDcc::HairBuffers;

class HairTranslator;

enum HairParam : ParamID {
	kWidthScale = 0,
	kDisplayPercent,
	kRenderPercent,
	kColorMode,      // 0 Auto, 1 Sisir Color, 2 Hair Set Color, 3 Material Color
	kSetColor,
	kMaterialId,
	kGroomRoughness, // what Sisir's ABC export writes for the set (from its render material)
	kGroomAo,
	kSourceFile,
	kHairSetName,
};

class HairObject : public GeomObject {
public:
	static ClassDesc2* classDesc();

	HairObject();
	~HairObject() override;

	std::shared_ptr<const HairBuffers> buffers() const { return m_buffers; }
	void setBuffers(std::shared_ptr<const HairBuffers> buffers);
	IParamBlock2* params() const { return m_pblock; }
	float paramFloat(ParamID id) const;
	int paramInt(ParamID id) const;
	Point3 paramColor(ParamID id) const;
	QString paramString(ParamID id) const;

	// Animatable
	Class_ID ClassID() override;
	SClass_ID SuperClassID() override { return GEOMOBJECT_CLASS_ID; }
	void GetClassName(MSTR& s, bool localized) const override;
	void DeleteThis() override { delete this; }
	int NumSubs() override { return 1; }
	Animatable* SubAnim(int i) override { return i == 0 ? m_pblock : nullptr; }
	MSTR SubAnimName(int i, bool localized) override;
	int NumParamBlocks() override { return 1; }
	IParamBlock2* GetParamBlock(int i) override { return i == 0 ? m_pblock : nullptr; }
	IParamBlock2* GetParamBlockByID(BlockID id) override;
	void BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) override;
	void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override;
	BaseInterface* GetInterface(Interface_ID id) override;
	using GeomObject::GetInterface;

	// ReferenceMaker / ReferenceTarget
	int NumRefs() override { return 1; }
	RefTargetHandle GetReference(int i) override { return i == 0 ? m_pblock : nullptr; }
	RefResult NotifyRefChanged(const Interval& changeInt, RefTargetHandle hTarget, PartID& partID, RefMessage message,
	                           BOOL propagate) override;
	RefTargetHandle Clone(RemapDir& remap) override;
	IOResult Save(ISave* isave) override;
	IOResult Load(ILoad* iload) override;

	// BaseObject
	CreateMouseCallBack* GetCreateMouseCallBack() override { return nullptr; }
	const MCHAR* GetObjectName(bool localized) const override;
	int HitTest(TimeValue t, INode* inode, int type, int crossing, int flags, IPoint2* p, ViewExp* vpt) override;
	int Display(TimeValue, INode*, ViewExp*, int) override { return 0; }
	void GetWorldBoundBox(TimeValue t, INode* inode, ViewExp* vp, Box3& box) override;
	void GetLocalBoundBox(TimeValue t, INode* inode, ViewExp* vp, Box3& box) override;
	unsigned long GetObjectDisplayRequirement() const override { return 0; }
	bool PrepareDisplay(const MaxSDK::Graphics::UpdateDisplayContext& context) override;
	bool UpdatePerNodeItems(const MaxSDK::Graphics::UpdateDisplayContext& updateDisplayContext,
	                        MaxSDK::Graphics::UpdateNodeContext& nodeContext,
	                        MaxSDK::Graphics::IRenderItemContainer& targetRenderItemContainer) override;

	// Object
	ObjectState Eval(TimeValue) override { return ObjectState(this); }
	Interval ObjectValidity(TimeValue) override { return FOREVER; }
	void InitNodeName(MSTR& s) override { s = _M("SisirHair"); }
	int CanConvertToType(Class_ID obtype) override { return obtype == ClassID() ? 1 : 0; }
	int IsRenderable() override { return TRUE; }
	BOOL IsWorldSpaceObject() override { return FALSE; }
	int IsDeformable() override { return FALSE; }
	int NumPoints() override { return 0; }
	void GetDeformBBox(TimeValue t, Box3& box, Matrix3* tm, BOOL useSel) override;

	// GeomObject: renderers without a Sisir translation see no surface (the hair renders in Arnold).
	Mesh* GetRenderMesh(TimeValue t, INode* inode, View& view, BOOL& needDelete) override;

private:
	void SetReference(int i, RefTargetHandle rtarg) override;
	Box3 localBounds() const;

	IParamBlock2* m_pblock = nullptr;
	std::shared_ptr<const HairBuffers> m_buffers;
	std::unique_ptr<HairTranslator> m_translator;
	std::map<INode*, MaxSDK::Graphics::ICustomRenderItem*> m_renderItems; // ref-counted, one per node
};

// The renderer-facing view of a Sisir Hair object's strands (object space, system units). Valid
// while the object keeps the same hair; `toRenderingSpace` is the scene's rendering space.
bool hairExportView(HairObject& hair, SisirHairExportView& out);

} // namespace SisirMax

// For renderer translators in other DLLs (common/HairExportView.h). False when `object` is not a
// Sisir Hair object or has no hair.
extern "C" __declspec(dllexport) bool sisirGetHairExportView(Object* object, SisirHairExportView* out);
