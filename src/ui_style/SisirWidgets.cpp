#include "SisirWidgets.h"

#include "ResetToDefault.h"

#include <QColorDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {
SisirWidgets::GestureHooks& gestureHooks() {
	static SisirWidgets::GestureHooks hooks;
	return hooks;
}

// glm::clamp's definition, so the plugins need no glm: min(max(v, lo), hi).
template <class T>
T clampTo(T v, T lo, T hi) { return std::min(std::max(v, lo), hi); }
} // namespace

void SisirWidgets::setSliderGestureHooks(GestureHooks hooks) { gestureHooks() = std::move(hooks); }


// `resetTo`, when given, is the factory default in the field's DISPLAYED units; the field then
// carries a Reset to Default arrow (ResetToDefault.h) that writes it through `onChange`.
QLineEdit* makeTextField(const QString& text) {
	auto* field = new QLineEdit(text);
	// The value boxes' look (NoWheelDoubleSpinBox under the Windows 11 style, sampled): the palette's
	// base colour inside a thin border, and no underline. The same height as a value box.
	field->setStyleSheet(QStringLiteral(
		"QLineEdit { background: #101214; color: #C9D1D9; border: 1px solid #2B3138; border-radius: 4px;"
		" padding: 2px 6px; selection-background-color: #3A7BFF; }"));
	field->setFixedHeight(NoWheelDoubleSpinBox().sizeHint().height());
	return field;
}

QWidget* makeFloatSlider(double min, double max, double step, int decimals, double value,
	const std::function<void(double)>& onChange, std::optional<double> resetTo) {
	auto* container = new QWidget();
	auto* layout = new QHBoxLayout(container);
	layout->setContentsMargins(0, 0, 0, 0);
	auto state = std::make_shared<FloatSliderState>();
	state->min = min;
	state->max = max;
	state->step = step;

	auto* slider = new NoWheelSlider(Qt::Horizontal);
	slider->setObjectName("slider");
	int steps = (int)std::round((max - min) / step);
	if (steps < 1) steps = 1;
	if (steps > 10000) steps = 10000;
	state->steps = steps;
	slider->setRange(0, steps);
	int sliderValue = (int)std::round((value - min) / step);
	slider->setValue(clampTo(sliderValue, 0, steps));

	auto* spin = new NoWheelDoubleSpinBox();
	spin->setObjectName("spin");
	spin->setRange(min, max);
	spin->setSingleStep(step);
	spin->setDecimals(decimals);
	spin->setValue(value);
	spin->setFixedWidth(72);
	spin->setKeyboardTracking(false); // one undo step per typed value, not per digit

	auto updating = std::make_shared<bool>(false);
	QObject::connect(slider, &QSlider::valueChanged, container, [=](int v) {
		if (*updating) return;
		*updating = true;
		double nv = state->min + (double)v * state->step;
		nv = clampTo(nv, state->min, state->max);
		spin->setValue(nv);
		if (onChange) onChange(nv);
		*updating = false;
	});
	QObject::connect(slider, &QSlider::sliderPressed, container, [container]() {
		if (gestureHooks().begin) gestureHooks().begin(container);
	});
	QObject::connect(slider, &QSlider::sliderReleased, container, [container]() {
		if (gestureHooks().end) gestureHooks().end(container);
	});
	QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), container, [=](double v) {
		if (*updating) return;
		*updating = true;
		double clamped = clampTo(v, state->min, state->max);
		int sv = (int)std::round((clamped - state->min) / state->step);
		slider->setValue(clampTo(sv, 0, state->steps));
		if (onChange) onChange(clamped);
		*updating = false;
	});
	container->setProperty("floatSliderState", QVariant::fromValue<void*>(state.get()));

	layout->addWidget(slider, 1);
	layout->addWidget(spin, 0);
	if (resetTo) {
		const double def = clampTo(*resetTo, min, max);
		// Compared at the precision the spin box shows, so float noise never raises the arrow.
		const double tolerance = 0.5 * std::pow(10.0, -decimals);
		auto* control = new UiReset::Control(container, layout,
			[spin, def, tolerance]() { return std::abs(spin->value() - def) > tolerance; },
			[=]() {
				*updating = true;
				slider->setValue(clampTo((int)std::round((def - state->min) / state->step), 0, state->steps));
				spin->setValue(def);
				*updating = false;
				// Called directly rather than through the spin box: a mixed selection can already be
				// SHOWING the default, and setValue would then not fire at all.
				if (onChange) onChange(def);
			},
			QString::number(def, 'f', decimals));
		QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), control,
			[control](double) { control->refresh(); });
	}
	return container;
}

// Push a value into a makeFloatSlider field from outside (a viewport shortcut, a scene reload)
// without re-entering its change handler. The slider's index mapping is read back off the spin
// box's own range and step, so callers never restate what makeFloatSlider already decided.
void setFloatSliderValue(QWidget* field, double value) {
	if (!field) return;
	auto* slider = field->findChild<QSlider*>("slider");
	auto* spin = field->findChild<QDoubleSpinBox*>("spin");
	if (!slider || !spin) return;
	const double clamped = clampTo(value, spin->minimum(), spin->maximum());
	const double step = (spin->singleStep() > 0.0) ? spin->singleStep() : 1.0;
	// Signals stay blocked: these setValue calls would otherwise run the onChange callback and
	// push the value straight back onto the scene it just came from.
	const QSignalBlocker sliderBlocker(slider);
	const QSignalBlocker spinBlocker(spin);
	const int sliderValue = (int)std::round((clamped - spin->minimum()) / step);
	slider->setValue(clampTo(sliderValue, 0, slider->maximum()));
	spin->setValue(clamped);
	UiReset::refresh(field); // the blocked signals would otherwise leave its arrow stale
}

QWidget* makeIntSlider(int min, int max, int value, const std::function<void(int)>& onChange,
	std::optional<int> resetTo) {
	auto* container = new QWidget();
	auto* layout = new QHBoxLayout(container);
	layout->setContentsMargins(0, 0, 0, 0);

	auto* slider = new NoWheelSlider(Qt::Horizontal);
	slider->setRange(min, max);
	slider->setValue(clampTo(value, min, max));

	auto* spin = new NoWheelSpinBox();
	spin->setRange(min, max);
	spin->setValue(value);
	spin->setFixedWidth(72);
	spin->setKeyboardTracking(false); // one undo step per typed value, not per digit

	auto updating = std::make_shared<bool>(false);
	QObject::connect(slider, &QSlider::valueChanged, container, [=](int v) {
		if (*updating) return;
		*updating = true;
		spin->setValue(v);
		if (onChange) onChange(v);
		*updating = false;
	});
	QObject::connect(slider, &QSlider::sliderPressed, container, [container]() {
		if (gestureHooks().begin) gestureHooks().begin(container);
	});
	QObject::connect(slider, &QSlider::sliderReleased, container, [container]() {
		if (gestureHooks().end) gestureHooks().end(container);
	});
	QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), container, [=](int v) {
		if (*updating) return;
		*updating = true;
		slider->setValue(v);
		if (onChange) onChange(v);
		*updating = false;
	});

	layout->addWidget(slider, 1);
	layout->addWidget(spin, 0);
	if (resetTo) {
		const int def = clampTo(*resetTo, min, max);
		auto* control = new UiReset::Control(container, layout,
			[spin, def]() { return spin->value() != def; },
			[=]() {
				*updating = true;
				slider->setValue(def);
				spin->setValue(def);
				*updating = false;
				if (onChange) onChange(def);
			},
			QString::number(def));
		QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), control,
			[control](int) { control->refresh(); });
	}
	return container;
}

// A colour swatch that opens a picker. With `resetTo` it sits in a row with a Reset to Default
// arrow; colours compare as the #rrggbb the swatch shows, so float noise never raises it.
QWidget* makeColorSwatch(QWidget* dialogParent, const float color[3],
	const std::function<void(const QColor&)>& onChange, const float* resetTo, int minWidthPx) {
	auto* button = new QToolButton();
	button->setText(" ");
	auto current = std::make_shared<QColor>(QColor::fromRgbF(color[0], color[1], color[2]));
	auto restyle = [button, minWidthPx](const QColor& c) {
		button->setStyleSheet(QString("background: %1; border-radius: 6px; min-width: %2px;").arg(c.name()).arg(minWidthPx));
	};
	restyle(*current);
	QObject::connect(button, &QToolButton::clicked, button, [=]() {
		const QColor selected = QColorDialog::getColor(*current, dialogParent, "Select Color");
		if (!selected.isValid()) return;
		*current = selected;
		restyle(selected);
		onChange(selected);
		UiReset::refresh(button->parentWidget());
	});
	if (!resetTo) return button;
	const QColor def = QColor::fromRgbF(resetTo[0], resetTo[1], resetTo[2]);
	auto* row = new QWidget();
	auto* layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(button);
	layout->addStretch(1);
	new UiReset::Control(row, layout,
		[current, def]() { return current->name() != def.name(); },
		[=]() { *current = def; restyle(def); onChange(def); },
		def.name());
	return row;
}

void setFormRowVisible(QFormLayout* form, QWidget* control, bool visible) {
	QWidget* field = control;
	while (field && form->indexOf(field) < 0) field = field->parentWidget();
	if (!field) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
	form->setRowVisible(field, visible);
#else
	// Qt 5 (Maya 2023/2024) has no setRowVisible: hide the row's two halves instead.
	if (QWidget* label = form->labelForField(field)) label->setVisible(visible);
	field->setVisible(visible);
#endif
}

QGroupBox* makeCollapsibleBox(const QString& title, bool* openState, QWidget** outContent) {
	auto* box = new QGroupBox();
	box->setTitle(QString());

	auto* boxLayout = new QVBoxLayout(box);
	boxLayout->setContentsMargins(6, 8, 6, 6);
	boxLayout->setSpacing(4);

	const bool open = openState ? *openState : true;
	auto* header = new QToolButton(box);
	header->setObjectName("CollapsibleHeader");
	header->setText(title);
	header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	header->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
	header->setCheckable(true);
	header->setChecked(open);
	header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	header->setAutoRaise(true);

	auto* content = new QWidget(box);
	// Named, so code can tell "folded away" from "hidden because it does not apply": a hotkey
	// still reaches a control in a collapsed section (QtMainWindowHotkeys.cpp).
	content->setObjectName("CollapsibleContent");
	content->setVisible(open);

	// Header and its section-reset arrow share a row. Both stay DIRECT children of the box, which is
	// how the header is looked up (findChild "CollapsibleHeader", direct children only).
	auto* headerRow = new QHBoxLayout();
	headerRow->setContentsMargins(0, 0, 0, 0);
	headerRow->setSpacing(0);
	headerRow->addWidget(header, 1);
	boxLayout->addLayout(headerRow);
	boxLayout->addWidget(content);
	UiReset::attachSection(box, headerRow, content, title);

	QObject::connect(header, &QToolButton::toggled, content, [header, content, openState](bool checked) {
		content->setVisible(checked);
		header->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
		if (openState) *openState = checked;
	});

	if (outContent) *outContent = content;
	return box;
}
