#include "HairTranslator.h"

#include "ArnoldCurves.h"
#include "HairObject.h"

#include <inode.h>

#include <string>

namespace SisirMax {

using namespace Max::ArnoldTranslator;

Arnold_translation::TranslationInterface_Result HairTranslator::Translate(INode* node, Arnold_translation* translation,
                                                                          const TimeValue, Interval& newValidity) {
	if (!node || !translation) return Arnold_translation::Failure;
	// One output per node; the name only has to be unique in the Arnold scene.
	const MSTR name = MSTR(_M("SisirHair_")) + MSTR::FromACP(std::to_string(node->GetHandle()).c_str());
	AtNode* curves = translation->GetOutput(0, AtString("curves"), AtString(name.ToUTF8().data()));
	if (!curves) return Arnold_translation::Failure;
	const Arnold_translation::TranslationInterface_Result generic =
		translation->PerformGenericTranslation(0, Arnold_translation::SkipFlag_None);
	if (generic != Arnold_translation::Success) return generic;
	SisirHairExportView view;
	if (hairExportView(m_hair, view)) SisirArnold::writeCurves(curves, view);
	else SisirArnold::writeEmptyCurves(curves);
	// The strands are fixed; parameter changes re-translate through MAXtoA's change tracking.
	newValidity &= FOREVER;
	return Arnold_translation::Success;
}

Interval HairTranslator::CheckValidity(const TimeValue, const Interval& previous) const { return previous; }

} // namespace SisirMax
