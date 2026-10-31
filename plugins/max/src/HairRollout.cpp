#include "HairRollout.h"

#include "HairObject.h"
#include "ImportDialog.h"
#include "MaxUtil.h"
#include "ResetToDefault.h"
#include "UiTooltips.h"
#include "ui_style/SisirWidgets.h"

#include <hold.h>

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

namespace SisirMax {

namespace {

QWidget* tip(QWidget* w, const char* id) {
	w->setToolTip(UiTooltips::text(id));
	return w;
}

} // namespace

HairRollout::HairRollout(IParamBlock2& block, QWidget* parent) : m_block(&block) {
	if (parent) setParent(parent);
	SisirDcc::applySisirLook(this);
	auto* v = new QVBoxLayout(this);
	v->setContentsMargins(4, 4, 4, 4);
	auto* form = new QFormLayout();
	form->setContentsMargins(0, 0, 0, 0);
	form->setLabelAlignment(Qt::AlignLeft);
	v->addLayout(form);

	m_widthScale = makeFloatSlider(0.0, 4.0, 0.01, 2, block.GetFloat(kWidthScale),
	                               [this](double x) { setFloat(kWidthScale, x, _M("Width Scale")); }, 1.0);
	form->addRow("Width Scale", tip(m_widthScale, "max.hair.widthScale"));
	m_displayPercent = makeFloatSlider(0.0, 100.0, 1.0, 0, block.GetFloat(kDisplayPercent),
	                                   [this](double x) { setFloat(kDisplayPercent, x, _M("Display %")); }, 100.0);
	form->addRow("Display %", tip(m_displayPercent, "max.hair.displayPercent"));
	m_renderPercent = makeFloatSlider(0.0, 100.0, 1.0, 0, block.GetFloat(kRenderPercent),
	                                  [this](double x) { setFloat(kRenderPercent, x, _M("Render %")); }, 100.0);
	form->addRow("Render %", tip(m_renderPercent, "max.hair.renderPercent"));

	m_colorMode = new NoWheelComboBox();
	m_colorMode->addItems({ "Auto", "Sisir Color", "Hair Set Color", "Material Color" });
	m_colorMode->setCurrentIndex(block.GetInt(kColorMode));
	tip(m_colorMode, "max.hair.colorMode");
	connect(m_colorMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
	        [this](int i) { setInt(kColorMode, i, _M("Color Mode")); });
	form->addRow("Color Mode", UiReset::combo(m_colorMode, 0));

	const Color c = block.GetColor(kSetColor);
	const float color[3] = { c.r, c.g, c.b };
	static const float kDefaultSetColor[3] = { 0.9f, 0.8f, 0.65f };
	m_setColorRow = makeColorSwatch(this, color, [this](const QColor& q) { setColor(kSetColor, q, _M("Hair Set Color")); },
	                                kDefaultSetColor);
	form->addRow("Set Color", tip(m_setColorRow, "max.hair.setColor"));

	// What the object holds, as data: the set, its size and where it came from.
	m_info = new QLabel(this);
	m_info->setObjectName("note");
	m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_info->setWordWrap(true);
	v->addWidget(m_info);
	UiReset::refreshSections(this);
	UpdateUI(GetCOREInterface()->GetTime());
}

void HairRollout::SetParamBlock(ReferenceMaker*, IParamBlock2* const block) {
	m_block = block;
	UpdateUI(GetCOREInterface()->GetTime());
}

void HairRollout::UpdateUI(const TimeValue t) {
	if (!m_block) return;
	m_updating = true;
	setFloatSliderValue(m_widthScale, m_block->GetFloat(kWidthScale, t));
	setFloatSliderValue(m_displayPercent, m_block->GetFloat(kDisplayPercent, t));
	setFloatSliderValue(m_renderPercent, m_block->GetFloat(kRenderPercent, t));
	{
		const QSignalBlocker block(m_colorMode);
		m_colorMode->setCurrentIndex(m_block->GetInt(kColorMode, t));
	}
	for (QWidget* w : { m_widthScale, m_displayPercent, m_renderPercent, static_cast<QWidget*>(m_colorMode) }) UiReset::refresh(w);
	QString info;
	if (auto* hair = dynamic_cast<HairObject*>(m_block->GetOwner())) {
		auto b = hair->buffers();
		const QString set = hair->paramString(kHairSetName);
		if (!set.isEmpty()) info += set + "\n";
		info += QString("%1 strands, %2 points").arg(QLocale().toString(b ? (qlonglong)b->strandCount() : 0))
		                                         .arg(QLocale().toString(b ? (qlonglong)b->pointCount() : 0));
		const QString source = hair->paramString(kSourceFile);
		if (!source.isEmpty()) m_info->setToolTip(source);
	}
	m_info->setText(info);
	m_updating = false;
}

void HairRollout::UpdateParameterUI(const TimeValue t, const ParamID, const int) { UpdateUI(t); }

void HairRollout::setFloat(ParamID id, double value, const MCHAR* undoName) {
	if (m_updating || !m_block) return;
	const bool own = !theHold.Holding();
	if (own) theHold.Begin();
	m_block->SetValue(id, GetCOREInterface()->GetTime(), (float)value);
	if (own) theHold.Accept(undoName);
	GetCOREInterface()->RedrawViews(GetCOREInterface()->GetTime());
}

void HairRollout::setInt(ParamID id, int value, const MCHAR* undoName) {
	if (m_updating || !m_block) return;
	const bool own = !theHold.Holding();
	if (own) theHold.Begin();
	m_block->SetValue(id, GetCOREInterface()->GetTime(), value);
	if (own) theHold.Accept(undoName);
	GetCOREInterface()->RedrawViews(GetCOREInterface()->GetTime());
}

void HairRollout::setColor(ParamID id, const QColor& value, const MCHAR* undoName) {
	if (m_updating || !m_block) return;
	const bool own = !theHold.Holding();
	if (own) theHold.Begin();
	m_block->SetValue(id, GetCOREInterface()->GetTime(), Color((float)value.redF(), (float)value.greenF(), (float)value.blueF()));
	if (own) theHold.Accept(undoName);
	GetCOREInterface()->RedrawViews(GetCOREInterface()->GetTime());
}

} // namespace SisirMax
