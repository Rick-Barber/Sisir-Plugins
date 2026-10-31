#pragma once

// Sisir's inspector controls: slider rows, value boxes, colour swatches and collapsible sections.
//
// Shared by the app's panels (through InspectorWidgets.h) and the DCC plugins' dialogs, so a
// plugin's options look and behave exactly like the app's inspector. Plain QtWidgets (Qt 5.15 and
// Qt 6) plus ResetToDefault; nothing app-specific -- a slider drag reaches Undo only through the
// gesture hooks below, which the app installs.

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QSpinBox>
#include <QString>
#include <QWheelEvent>

#include <functional>
#include <optional>

class QColor;
class QFormLayout;
class QGroupBox;
class QLineEdit;
class QWidget;

// Value controls the mouse wheel does not change: scrolling a panel must never edit a value the
// cursor happens to pass over.
class NoWheelComboBox : public QComboBox {
public:
	NoWheelComboBox() = default;
protected:
	void wheelEvent(QWheelEvent* event) override { event->ignore(); }
};

class NoWheelDoubleSpinBox : public QDoubleSpinBox {
public:
	NoWheelDoubleSpinBox() {
		setButtonSymbols(QAbstractSpinBox::NoButtons);
		setKeyboardTracking(false);
	}
protected:
	void wheelEvent(QWheelEvent* event) override { event->ignore(); }
};

class NoWheelSpinBox : public QSpinBox {
public:
	NoWheelSpinBox() {
		setButtonSymbols(QAbstractSpinBox::NoButtons);
		setKeyboardTracking(false);
	}
protected:
	void wheelEvent(QWheelEvent* event) override { event->ignore(); }
};

class NoWheelSlider : public QSlider {
public:
	explicit NoWheelSlider(Qt::Orientation orientation) : QSlider(orientation) {}
protected:
	void wheelEvent(QWheelEvent* event) override { event->ignore(); }
};

namespace SisirWidgets {
// Called when a slider row's handle is pressed / released, with the row's container, so a whole
// drag can become one Undo step. Unset, a drag is just a series of value changes.
struct GestureHooks {
	std::function<void(QWidget*)> begin;
	std::function<void(QWidget*)> end;
};
void setSliderGestureHooks(GestureHooks hooks);
} // namespace SisirWidgets

// A makeFloatSlider field's range, reachable through its "floatSliderState" property (a void*) for
// code that re-ranges the field in place.
struct FloatSliderState {
	double min = 0.0;
	double max = 1.0;
	double step = 0.01;
	int steps = 100;
};

// A slider + spin box pair. `resetTo`, when given, is the factory default in the field's DISPLAYED
// units; the field then carries a Reset to Default arrow (ResetToDefault.h).
QWidget* makeFloatSlider(double min, double max, double step, int decimals, double value,
	const std::function<void(double)>& onChange, std::optional<double> resetTo = std::nullopt);
// Pushes a value into a makeFloatSlider field without re-entering its change handler.
void setFloatSliderValue(QWidget* field, double value);
QWidget* makeIntSlider(int min, int max, int value, const std::function<void(int)>& onChange,
	std::optional<int> resetTo = std::nullopt);
// A free-text field that looks like the slider value boxes: the native Windows 11 style draws a
// plain QLineEdit with an accent underline, which no other inspector field has.
QLineEdit* makeTextField(const QString& text);
QWidget* makeColorSwatch(QWidget* dialogParent, const float color[3],
	const std::function<void(const QColor&)>& onChange, const float* resetTo, int minWidthPx = 18);
// QFormLayout::setRowVisible for a control that may sit inside a wrapper -- a Reset to Default row,
// a "(mixed)" row, a tooltip host: the row is found through the control's nearest ancestor that is
// one of the form's own fields. Handing setRowVisible the wrapped control itself does nothing but
// print "Invalid widget".
void setFormRowVisible(QFormLayout* form, QWidget* control, bool visible);
// A titled box that collapses on its header; openState (optional) remembers the state.
QGroupBox* makeCollapsibleBox(const QString& title, bool* openState, QWidget** outContent);
