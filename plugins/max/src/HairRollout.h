#pragma once

// The Sisir Hair object's Modify panel rollout, in Sisir's own style: the attributes Maya's
// Attribute Editor shows for sisirHairShape, each with its Reset to Default arrow, explanations in
// tooltips only. Edits go through the parameter block, so they undo like any Max parameter.

#include <Qt/QMaxParamBlockWidget.h>

class QComboBox;
class QLabel;
class QWidget;

namespace SisirMax {

class HairRollout : public MaxSDK::QMaxParamBlockWidget {
	Q_OBJECT
public:
	explicit HairRollout(IParamBlock2& block, QWidget* parent = nullptr);

	void SetParamBlock(ReferenceMaker* owner, IParamBlock2* const block) override;
	void UpdateUI(const TimeValue t) override;
	void UpdateParameterUI(const TimeValue t, const ParamID id, const int tabIndex) override;

private:
	void setFloat(ParamID id, double value, const MCHAR* undoName);
	void setInt(ParamID id, int value, const MCHAR* undoName);
	void setColor(ParamID id, const QColor& value, const MCHAR* undoName);

	IParamBlock2* m_block;
	bool m_updating = false;
	QWidget* m_widthScale = nullptr;
	QWidget* m_displayPercent = nullptr;
	QWidget* m_renderPercent = nullptr;
	QComboBox* m_colorMode = nullptr;
	QWidget* m_setColorRow = nullptr;
	QLabel* m_info = nullptr;
};

} // namespace SisirMax
