#include "SisirStyle.h"

QPalette SisirStyle::palette() {
	QPalette p;
	const QColor accent(kAccentGuides);
	p.setColor(QPalette::Window,          kWindow);
	p.setColor(QPalette::WindowText,      kText);
	p.setColor(QPalette::Base,            kBase);
	p.setColor(QPalette::AlternateBase,   kPanel);
	p.setColor(QPalette::ToolTipBase,     kBase);
	p.setColor(QPalette::ToolTipText,     kText);
	p.setColor(QPalette::Text,            kText);
	p.setColor(QPalette::Button,          kPanel);
	p.setColor(QPalette::ButtonText,      kText);
	p.setColor(QPalette::BrightText,      Qt::white);
	p.setColor(QPalette::Link,            accent);
	p.setColor(QPalette::Highlight,       accent);
	p.setColor(QPalette::HighlightedText, Qt::white);
	p.setColor(QPalette::Disabled, QPalette::Text,       kMuted);
	p.setColor(QPalette::Disabled, QPalette::ButtonText, kMuted);
	return p;
}

QString SisirStyle::styleSheet(const QString& accent) {
	return QString(
		"QMainWindow { background: #16191D; }"
		"QDockWidget { titlebar-close-icon: none; titlebar-normal-icon: none; }"
		"QDockWidget::title { background: #1F2328; padding: 6px; color: #C9D1D9; }"
		"QLabel#moduleLabel { font-size: 16px; font-weight: 600; color: %1; padding: 4px 0px; }"
		"QGroupBox { border: 1px solid #2B3138; border-radius: 8px; margin-top: 8px; color: #C9D1D9; }"
		"QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
		"QToolButton#CollapsibleHeader { background: %1; color: #FFFFFF; border-radius: 6px; padding: 4px 8px; }"
		"QToolButton#CollapsibleHeader:hover { background: %1; }"
		"QCheckBox, QRadioButton, QLabel { color: #C9D1D9; }"
		"QCheckBox::indicator, QRadioButton::indicator { width: 12px; height: 12px; border-radius: 3px; border: 1px solid #2B3138; background: #101214; }"
		"QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: %1; border: 1px solid %1; }"
		"QSpinBox, QDoubleSpinBox, QComboBox { background: #101214; color: #C9D1D9; border: 1px solid #2B3138; border-radius: 6px; padding: 2px 6px; }"
		"QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 18px; border-left: 1px solid #2B3138; }"
		"QComboBox::down-arrow { width: 0px; height: 0px; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 7px solid %1; }"
		"QSlider::groove:horizontal { height: 6px; background: #2B3138; border-radius: 3px; }"
		"QSlider::sub-page:horizontal { background: %1; border-radius: 3px; }"
		"QSlider::handle:horizontal { width: 12px; margin: -4px 0; border-radius: 6px; background: %1; }"
		"QPushButton { background: #1F2328; color: #C9D1D9; border: 1px solid #2B3138; border-radius: 6px; padding: 2px 8px; }"
		"QPushButton:hover { background: #2B3138; color: #C9D1D9; }"
		"QPushButton:pressed { background: #101214; }"
		"QToolButton { color: #C9D1D9; border-radius: 6px; }"
		"QToolButton:hover { background: #2B3138; color: #C9D1D9; }"
		"QToolBar { background: #1F2328; spacing: 6px; padding: 4px; }"
		"QToolBar QToolButton { color: #C9D1D9; padding: 4px 10px; border-radius: 6px; }"
		"QToolBar QToolButton:hover { background: #2B3138; color: #C9D1D9; }"
		"QToolBar QToolButton:checked { background: %1; color: #FFFFFF; }"
		"QAbstractItemView { background: #101214; color: #C9D1D9; border: 1px solid #2B3138; alternate-background-color: #16191D; gridline-color: #2B3138; }"
		"QAbstractItemView::item:hover { background: #1F2328; color: #C9D1D9; }"
		"QAbstractItemView::item:selected { background: %1; color: #FFFFFF; }"
		"QAbstractItemView::item:selected:hover { background: %1; color: #FFFFFF; }"
		"QHeaderView::section { background: #1F2328; color: #C9D1D9; border: 1px solid #2B3138; padding: 4px; }"
		"QScrollBar:vertical { background: #16191D; width: 10px; border-radius: 5px; }"
		"QScrollBar::handle:vertical { background: #2B3138; border-radius: 5px; min-height: 20px; }"
		"QScrollBar::handle:vertical:hover { background: #586069; }"
		"QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
		"QScrollBar:horizontal { background: #16191D; height: 10px; border-radius: 5px; }"
		"QScrollBar::handle:horizontal { background: #2B3138; border-radius: 5px; min-width: 20px; }"
		"QScrollBar::handle:horizontal:hover { background: #586069; }"
		"QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
		"QMenuBar { background: #1F2328; color: #C9D1D9; }"
		"QMenuBar::item { background: transparent; padding: 4px 8px; }"
		"QMenuBar::item:selected { background: #2B3138; }"
		"QMenuBar::item:pressed { background: #2B3138; }"
		"QMenu { background: #1F2328; color: #C9D1D9; border: 1px solid #2B3138; }"
		"QMenu::item { padding: 4px 24px 4px 12px; }"
		"QMenu::item:selected { background: %1; color: #FFFFFF; }"
		"QMenu::separator { height: 1px; background: #2B3138; margin: 4px 0px; }"
	).arg(accent);
}
