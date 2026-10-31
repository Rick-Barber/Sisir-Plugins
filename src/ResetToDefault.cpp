#include "ResetToDefault.h"

#include "UiTooltips.h"

#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QLayoutItem>
#include <QTimer>
#include <QToolButton>

#include <cmath>

namespace UiReset {

namespace {

constexpr int kSlotWidth = 18;
constexpr int kButtonSize = 16;

UndoHooks& hooks() {
	static UndoHooks h;
	return h;
}

// The circular arrow, drawn once in code rather than shipped as an asset: no SVG plugin to deploy,
// and it picks up the panel's muted grey so it reads as secondary to the value beside it.
const QIcon& arrowIcon() {
	static const QIcon icon = [] {
		QIcon result;
		for (int dpr : { 1, 2 }) {
			QPixmap pm(kButtonSize * dpr, kButtonSize * dpr);
			pm.setDevicePixelRatio(dpr);
			pm.fill(Qt::transparent);
			QPainter p(&pm);
			p.setRenderHint(QPainter::Antialiasing, true);
			const QColor color(0x9A, 0xA4, 0xAF);
			const QPointF c(kButtonSize * 0.5, kButtonSize * 0.5);
			const double r = 4.6;
			// Counter-clockwise arc, open at the top right, with the head on its leading end.
			const double startDeg = 60.0, spanDeg = 270.0;
			p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap));
			p.drawArc(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), (int)(startDeg * 16), (int)(spanDeg * 16));
			const double th = (startDeg + spanDeg) * 3.14159265358979 / 180.0;
			const QPointF tip(c.x() + r * std::cos(th), c.y() - r * std::sin(th));
			const QPointF along(-std::sin(th), -std::cos(th)); // CCW tangent, screen space
			const QPointF out(std::cos(th), -std::sin(th));    // outward normal
			QPainterPath head;
			head.moveTo(tip + along * 2.4);
			head.lineTo(tip + out * 2.2);
			head.lineTo(tip - out * 2.2);
			head.closeSubpath();
			p.setPen(Qt::NoPen);
			p.setBrush(color);
			p.drawPath(head);
			p.end();
			result.addPixmap(pm);
		}
		return result;
	}();
	return icon;
}

} // namespace

QToolButton* makeArrowButton(QWidget* parent, const QString& tip) {
	auto* button = new QToolButton(parent);
	button->setObjectName("resetToDefault");
	button->setAutoRaise(true);
	button->setFocusPolicy(Qt::NoFocus);
	button->setIcon(arrowIcon());
	button->setIconSize(QSize(kButtonSize - 4, kButtonSize - 4));
	button->setFixedSize(kButtonSize, kButtonSize);
	button->setToolTip(tip);
	return button;
}

namespace {

// Resets a set of controls as ONE undo step. The batch is closed from the event loop, after any
// handler the resets deferred with a zero timer of its own (those were queued first, so run first).
void runReset(const QString& name, const QList<QPointer<Control>>& controls, void (Control::*apply)()) {
	if (hooks().beginBatch) hooks().beginBatch(name);
	for (const QPointer<Control>& control : controls) {
		if (control) (control.data()->*apply)();
	}
	QTimer::singleShot(0, qApp, [] { if (hooks().endBatch) hooks().endBatch(); });
}

// The arrow on a collapsible section's header.
class Section : public QObject {
public:
	Section(QGroupBox* box, QBoxLayout* headerRow, QWidget* content, const QString& title)
		: QObject(box), m_content(content), m_title(title) {
		m_button = makeArrowButton(box, UiTooltips::text("common.resetSection"));
		// Hidden keeps its space, like Unreal's NonVisibleState: the header must not shift under
		// the cursor the moment the section returns to default.
		QSizePolicy policy = m_button->sizePolicy();
		policy.setRetainSizeWhenHidden(true);
		m_button->setSizePolicy(policy);
		m_button->setVisible(false);
		headerRow->addWidget(m_button, 0, Qt::AlignVCenter);
		connect(m_button, &QToolButton::clicked, this, [this]() { resetAll(); });
	}

	void scheduleUpdate() {
		if (m_pending) return;
		m_pending = true;
		QTimer::singleShot(0, this, [this]() { m_pending = false; update(); });
	}

	void update() {
		bool any = false;
		for (Control* control : m_content->findChildren<Control*>()) {
			if (control->differs()) { any = true; break; }
		}
		m_button->setVisible(any);
	}

private:
	void resetAll() {
		QList<QPointer<Control>> controls;
		for (Control* control : m_content->findChildren<Control*>()) {
			if (control->differs()) controls.push_back(control);
		}
		if (controls.isEmpty()) return;
		runReset(QStringLiteral("Reset %1").arg(m_title), controls, &Control::reset);
	}

	QWidget* m_content;
	QString m_title;
	QToolButton* m_button = nullptr;
	bool m_pending = false;
};

// Every section box between `w` and its window, nearest first.
template <class Fn>
void forEachSection(QWidget* w, Fn fn) {
	for (QWidget* p = w; p; p = p->parentWidget()) {
		if (auto* box = qobject_cast<QGroupBox*>(p)) {
			for (QObject* child : box->children()) {
				if (auto* section = dynamic_cast<Section*>(child)) fn(section);
			}
		}
	}
}

} // namespace

// The arrow's place in its row: ONE layout item for the row's whole life, with or without the
// button. The button is not a layout item of its own -- the slot positions it -- so the layout
// cannot space the row differently once the arrow exists (a plain spacer gets no layout spacing,
// a widget does), and showing or hiding the arrow never triggers a relayout.
class ArrowSlot : public QLayoutItem {
public:
	QSize sizeHint() const override { return QSize(kSlotWidth, kButtonSize); }
	QSize minimumSize() const override { return sizeHint(); }
	QSize maximumSize() const override { return QSize(kSlotWidth, QLAYOUTSIZE_MAX); }
	Qt::Orientations expandingDirections() const override { return {}; }
	// Spaced exactly as the button would be, in either state.
	bool isEmpty() const override { return false; }
	QSizePolicy::ControlTypes controlTypes() const override { return QSizePolicy::ToolButton; }
	QRect geometry() const override { return m_rect; }
	void setGeometry(const QRect& rect) override {
		m_rect = rect;
		place();
	}

	void setButton(QToolButton* button) {
		m_button = button;
		place();
	}

private:
	// Against the slot's far edge, so the spare width is a margin on the value's side.
	void place() {
		if (!m_button) return;
		m_button->move(m_rect.right() + 1 - kButtonSize, m_rect.center().y() + 1 - kButtonSize / 2);
	}

	QRect m_rect;
	QPointer<QToolButton> m_button;
};

Control::Control(QWidget* field, QBoxLayout* slotLayout, std::function<bool()> differs,
                 std::function<void()> apply, QString defaultText)
	: QObject(field), m_field(field), m_slotLayout(slotLayout), m_differs(std::move(differs)),
	  m_apply(std::move(apply)), m_defaultText(std::move(defaultText)) {
	m_slot = new ArrowSlot();
	m_slotLayout->addItem(m_slot);
	refresh();
}

void Control::ensureButton() {
	if (m_button) return;
	// A child of the widget the layout arranges: the slot's rect is in that widget's coordinates.
	QWidget* row = m_slotLayout->parentWidget() ? m_slotLayout->parentWidget() : m_field;
	m_button = makeArrowButton(row, UiTooltips::text("common.resetToDefault", m_defaultText));
	m_slot->setButton(m_button);
	connect(m_button, &QToolButton::clicked, this, [this]() {
		runReset(QStringLiteral("Reset to Default"), { QPointer<Control>(this) }, &Control::reset);
	});
}

void Control::refresh() {
	const bool show = differs();
	if (show) ensureButton();
	if (m_button) m_button->setVisible(show);
	if (show != m_shown) {
		m_shown = show;
		notifySection();
	}
}

void Control::setMixed(bool mixed) {
	m_mixed = mixed;
	refresh();
}

void Control::reset() {
	if (!differs()) return;
	m_mixed = false; // written through, so the selection now agrees
	if (m_apply) m_apply();
	refresh();
}

void Control::notifySection() {
	forEachSection(m_field, [](Section* section) { section->scheduleUpdate(); });
}

Control* controlOf(QWidget* field) {
	if (!field) return nullptr;
	if (auto* own = field->findChild<Control*>(QString(), Qt::FindDirectChildrenOnly)) return own;
	return field->findChild<Control*>();
}

void refresh(QWidget* field) {
	if (Control* control = controlOf(field)) control->refresh();
}

void setMixed(QWidget* field, bool mixed) {
	if (Control* control = controlOf(field)) control->setMixed(mixed);
}

QWidget* check(QCheckBox* box, bool defaultValue) {
	auto* row = new QWidget();
	auto* layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(box);
	layout->addStretch(1);
	auto* control = new Control(row, layout,
		[box, defaultValue]() { return box->isChecked() != defaultValue; },
		[box, defaultValue]() { box->setChecked(defaultValue); },
		defaultValue ? QStringLiteral("On") : QStringLiteral("Off"));
	QObject::connect(box, &QCheckBox::toggled, control, [control](bool) { control->refresh(); });
	return row;
}

QWidget* combo(QComboBox* box, int defaultIndex) {
	auto* row = new QWidget();
	auto* layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(box, 1);
	auto* control = new Control(row, layout,
		[box, defaultIndex]() { return box->currentIndex() != defaultIndex; },
		[box, defaultIndex]() { box->setCurrentIndex(defaultIndex); },
		box->itemText(defaultIndex));
	QObject::connect(box, qOverload<int>(&QComboBox::currentIndexChanged), control,
		[control](int) { control->refresh(); });
	return row;
}

void attachSection(QGroupBox* box, QBoxLayout* headerRow, QWidget* content, const QString& title) {
	new Section(box, headerRow, content, title);
}

void refreshSections(QWidget* root) {
	if (!root) return;
	for (QGroupBox* box : root->findChildren<QGroupBox*>()) {
		for (QObject* child : box->children()) {
			if (auto* section = dynamic_cast<Section*>(child)) section->update();
		}
	}
}

void setUndoHooks(UndoHooks h) {
	hooks() = std::move(h);
}

} // namespace UiReset
