#pragma once

// Reset to Default: the small circular arrow beside a value, the way Unreal's details panel does it
// (SResetToDefaultPropertyEditor).
//
//   - The arrow shows only while the value differs from its FACTORY default -- what a new scene
//     starts with -- and keeps its space while hidden, so rows never shift as it comes and goes.
//   - Clicking it writes the default through the field's OWN edit path (the slider's onChange, the
//     checkbox's toggled handler), so selection, mirror twins, layer defaults and Undo behave
//     exactly as if the user had typed the value. Nothing here knows what a setting means.
//   - Each collapsible section carries one on its header too, shown while anything inside it
//     differs; it resets the whole section as a single Undo step.
//
// Cost: state is event-driven (no polling), and the arrow button itself is only created the first
// time its value differs, so a freshly built inspector with everything at default adds nothing but
// one fixed 18px layout slot per row.

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class QBoxLayout;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QToolButton;
class QWidget;

namespace UiReset {

class ArrowSlot;

// One resettable value. Lives as a child of the field it resets, which is how the field, its row
// and its section find it.
class Control : public QObject {
	Q_OBJECT
public:
	// `slotLayout` is where the arrow goes (a fixed slot holds its place, arrow or not).
	// `differs` reports whether the shown value is off its default; `apply` writes the default.
	Control(QWidget* field, QBoxLayout* slotLayout, std::function<bool()> differs,
	        std::function<void()> apply, QString defaultText);

	// Re-reads the value. Call after anything changes the field with its signals blocked.
	void refresh();
	// A mixed multi-selection always offers the reset, even if the shown value is the default.
	void setMixed(bool mixed);
	bool differs() const { return m_mixed || (m_differs && m_differs()); }
	// Writes the default through the field's own edit path.
	void reset();

private:
	void ensureButton();
	void notifySection();

	QWidget* m_field;
	QBoxLayout* m_slotLayout;
	ArrowSlot* m_slot = nullptr; // owned by m_slotLayout
	QToolButton* m_button = nullptr;
	std::function<bool()> m_differs;
	std::function<void()> m_apply;
	QString m_defaultText;
	bool m_mixed = false;
	bool m_shown = false;
};

// The Control attached to `field` (the field itself or any widget inside it), or null.
Control* controlOf(QWidget* field);
// For code that sets a field with its signals blocked.
void refresh(QWidget* field);
void setMixed(QWidget* field, bool mixed);

// Wrap a checkbox / combo in a row [control ... arrow] and return the row, to be added to the
// layout in the control's place. The control's own toggled / index handler does the work.
QWidget* check(QCheckBox* box, bool defaultValue);
QWidget* combo(QComboBox* combo, int defaultIndex);

// The bare arrow button, same look, for a list that manages its own defaults (the Hotkeys editor).
// No Control and no Undo hooks: the caller connects clicked.
QToolButton* makeArrowButton(QWidget* parent, const QString& tip);

// Section arrow on a collapsible box header. `headerRow` is the layout holding the header button.
void attachSection(QGroupBox* box, QBoxLayout* headerRow, QWidget* content, const QString& title);
// Sets every section arrow under `root` from the current values. Call once after building a panel.
void refreshSections(QWidget* root);

// How a reset reaches Undo. A section reset brackets its controls in one batch; a single-control
// reset uses the same bracket so it gets a readable name. Installed once by the main window.
struct UndoHooks {
	std::function<void(const QString&)> beginBatch;
	std::function<void()> endBatch;
};
void setUndoHooks(UndoHooks hooks);

} // namespace UiReset
