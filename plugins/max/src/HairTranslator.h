#pragma once

// Renders a Sisir Hair object in Arnold: MAXtoA's generic translator finds this interface on the
// object (HairObject::GetInterface) and calls Translate, which writes one Arnold `curves` node with
// common/ArnoldCurves.h -- exactly the curves Sisir for Maya's SisirMtoA writes. MAXtoA does the
// transform, the material and the render flags (PerformGenericTranslation).

#include <max.h>

#include <MAXtoA_TranslationInterface.h>

namespace SisirMax {

class HairObject;

class HairTranslator : public Max::ArnoldTranslator::MAXtoA_TranslationInterface {
public:
	explicit HairTranslator(HairObject& hair) : m_hair(hair) {}

	Max::ArnoldTranslator::Arnold_translation::TranslationInterface_Result Translate(
		INode* node, Max::ArnoldTranslator::Arnold_translation* translation, const TimeValue t,
		Interval& newValidity) override;

	Interval CheckValidity(const TimeValue t, const Interval& previous) const override;

private:
	HairObject& m_hair;
};

} // namespace SisirMax
