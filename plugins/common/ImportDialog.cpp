#include "ImportDialog.h"
#include "MetaFolders.h"

#include "HostInfo.h"
#include "ResetToDefault.h"
#include "UiTooltips.h"
#include "ui_style/SisirStyle.h"
#include "ui_style/SisirWidgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsOpacityEffect>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QCloseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace SisirDcc {

namespace {

// What the app's stylesheet leaves to the palette or never needed: the dialog's own background,
// text fields, list check boxes and the progress bar -- in the same colours.
const char* kPluginExtras =
	"QDialog { background: #16191D; }"
	"QScrollArea, QScrollArea > QWidget > QWidget { background: #16191D; border: none; }"
	"QLineEdit { background: #101214; color: #C9D1D9; border: 1px solid #2B3138; border-radius: 4px; padding: 2px 6px; selection-background-color: #3A7BFF; }"
	"QListWidget::item { padding: 0px 4px; }"
	"QListWidget::item:hover { background: transparent; }"
	"QListWidget QCheckBox { spacing: 8px; }"
	"QPushButton#browse { padding: 2px 0px; }"
		"QPushButton#primary { background: #3A7BFF; color: #FFFFFF; border: 1px solid #3A7BFF; padding: 4px 16px; font-weight: 600; }"
	"QPushButton#primary:hover { background: #5590FF; }"
	"QPushButton#primary:pressed { background: #2F66D6; }"
	"QPushButton#primary:disabled { background: #22272E; color: #6E7681; border: 1px solid #2B3138; }"
	"QPushButton { padding: 4px 12px; }"
	"QLabel#sceneTitle { font-size: 16px; font-weight: 600; color: #3A7BFF; padding: 4px 0px; }"
	"QLabel#note { color: #8B949E; }"
	"QProgressBar { background: #101214; border: 1px solid #2B3138; border-radius: 3px; }"
	"QProgressBar::chunk { background: #3A7BFF; border-radius: 2px; }";

QWidget* tip(QWidget* w, const char* id) {
	w->setToolTip(UiTooltips::text(id));
	return w;
}

QWidget* browseRow(QLineEdit* field, QWidget* parent, bool folder, const QString& filter = QString()) {
	auto* row = new QWidget(parent);
	auto* h = new QHBoxLayout(row);
	h->setContentsMargins(0, 0, 0, 0);
	h->setSpacing(4);
	h->addWidget(field, 1);
	auto* browse = new QPushButton("...", row);
	browse->setObjectName("browse");
	browse->setFixedWidth(28);
	h->addWidget(browse);
	QObject::connect(browse, &QPushButton::clicked, row, [=] {
		const QString start = field->text();
		const QString picked = folder ? QFileDialog::getExistingDirectory(row, QString(), start)
		                              : QFileDialog::getOpenFileName(row, QString(), start, filter);
		if (!picked.isEmpty()) field->setText(QDir::toNativeSeparators(picked));
	});
	return row;
}

} // namespace

void applySisirLook(QWidget* w) {
	w->setPalette(SisirStyle::palette());
	w->setAutoFillBackground(true);
	QFont font("Segoe UI", 9);
	w->setFont(font);
	w->setStyleSheet(SisirStyle::styleSheet(SisirStyle::kAccentGuides) + kPluginExtras);
}

QPointer<ImportDialog> ImportDialog::s_active;

ImportDialog::ImportDialog(const ImportOptions& options, const QString& sourceFile, QWidget* parent)
	: QDialog(parent), m_options(options), m_displayValue(options.displayPercent) {
	setWindowTitle("Sisir Import");
	setModal(true);
	applySisirLook(this);
	buildFrame(sourceFile);
	buildBody();
	setProgress(0.0f, "Generating hair and cards in Sisir...");
	resize(1000, 620);
	// Windows puts a new window on screen before Qt has painted it, and this one is shown from
	// inside a host command, so it would sit there white until the command returns. It stays
	// transparent until its first paint (paintEvent) instead.
	setWindowOpacity(0.0);
	QTimer::singleShot(500, this, [this] { setWindowOpacity(1.0); }); // never left invisible
	s_active = this;
}

void ImportDialog::paintEvent(QPaintEvent* event) {
	QDialog::paintEvent(event);
	if (m_revealed) return;
	m_revealed = true;
	// Revealed on the next turn of the event loop, once this paint has reached the screen.
	QTimer::singleShot(0, this, [this] { setWindowOpacity(1.0); });
}

ImportDialog* ImportDialog::active() { return s_active.data(); }

void ImportDialog::setMeta(const QJsonObject& meta) {
	m_meta = meta;
	m_pending = false;
	m_version->setText(QString("Sisir %1").arg(m_meta.value("sisirVersion").toString()));
	buildBody();
	m_progress->hide();
	m_status->hide();
	m_import->setEnabled(true);
	m_import->setFocus();
}

void ImportDialog::setProgress(float fraction, const QString& label) {
	if (fraction >= 0.0f) m_progress->setValue((int)(std::clamp(fraction, 0.0f, 1.0f) * 1000.0f));
	if (!label.isEmpty()) m_status->setText(label);
	m_progress->show();
	m_status->show();
}

void ImportDialog::setBuilding() {
	m_building = true;
	m_body->setEnabled(false);
	auto* dim = new QGraphicsOpacityEffect(m_body);
	dim->setOpacity(0.4);
	m_body->setGraphicsEffect(dim);
	m_import->setEnabled(false);
	m_cancel->setEnabled(false);
	setProgress(0.0f, QString("Building the %1 scene...").arg(host().displayName));
}

void ImportDialog::showBuildProgress(float fraction, const QString& label) {
	setProgress(fraction, label);
	// Painted now: the build runs inside a host command, so no event loop turns until it ends.
	m_progress->repaint();
	m_status->repaint();
}

void ImportDialog::closeEvent(QCloseEvent* event) {
	// While the host builds the scene the dialog stays up as its progress; the build closes it.
	if (m_building) {
		event->ignore();
		return;
	}
	QDialog::closeEvent(event);
}

void ImportDialog::reject() {
	if (!m_building) QDialog::reject();
}

void ImportDialog::finishBuilding() {
	m_building = false;
	close();
}

QListWidget* ImportDialog::makeList(const QJsonArray& entries, const QString& countKey, const QString& countLabel) {
	auto* list = new QListWidget(this);
	list->setSelectionMode(QAbstractItemView::NoSelection);
	list->setFocusPolicy(Qt::NoFocus);
	for (const QJsonValue& v : entries) {
		const QJsonObject o = v.toObject();
		const QString name = o.value("name").toString();
		// What Sisir showed: a layer in a hidden folder counts as hidden. Hair sets have no folders.
		const bool visible = metaLayerShown(o);
		QString text = metaFolderPrefix(o) + name;
		const double count = o.value(countKey).toDouble(-1);
		if (count >= 0) text += QString("   %1 %2").arg(QLocale().toString((qlonglong)count), countLabel);
		if (!visible) text += "   (hidden)";
		// A real check box per row, styled like every other check box in the dialog (the list's own
		// item indicators overlap their text under the native Windows style).
		auto* item = new QListWidgetItem(list);
		item->setFlags(Qt::ItemIsEnabled);
		auto* box = new QCheckBox(text);
		box->setProperty("sisirName", name);
		box->setProperty("sisirVisible", visible);
		box->setChecked(visible || m_options.includeHidden);
		item->setSizeHint(box->sizeHint() + QSize(0, 4));
		list->setItemWidget(item, box);
	}
	list->setMinimumHeight(90);
	list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	return list;
}

QStringList ImportDialog::checkedNames(const QListWidget* list, bool* allVisibleOnly) {
	QStringList names;
	bool sameAsDefault = true;
	for (int i = 0; i < list->count(); i++) {
		const auto* box = qobject_cast<const QCheckBox*>(list->itemWidget(list->item(i)));
		if (!box) continue;
		const bool on = box->isChecked();
		if (on) names << box->property("sisirName").toString();
		if (on != box->property("sisirVisible").toBool()) sameAsDefault = false;
	}
	if (allVisibleOnly) *allVisibleOnly = sameAsDefault;
	return names;
}

void ImportDialog::buildFrame(const QString& sourceFile) {
	m_outer = new QVBoxLayout(this);
	m_outer->setContentsMargins(10, 10, 10, 10);
	m_outer->setSpacing(8);

	auto* title = new QLabel(QFileInfo(sourceFile).completeBaseName(), this);
	title->setObjectName("sceneTitle");
	m_outer->addWidget(title);
	m_version = new QLabel(this);
	m_version->setObjectName("note");
	m_version->setToolTip(sourceFile);
	m_outer->addWidget(m_version);

	m_body = new QWidget(this);
	m_outer->addWidget(m_body, 1);

	auto* buttons = new QHBoxLayout();
	m_progress = new QProgressBar(this);
	m_progress->setRange(0, 1000);
	m_progress->setTextVisible(false);
	m_progress->setFixedHeight(8);
	m_progress->setFixedWidth(260);
	m_status = new QLabel(this);
	m_status->setObjectName("note");
	buttons->addWidget(m_progress, 0, Qt::AlignVCenter);
	buttons->addSpacing(8);
	buttons->addWidget(m_status);
	buttons->addStretch(1); // keeps the buttons right-aligned once the progress is hidden
	m_cancel = new QPushButton("Cancel", this);
	m_import = new QPushButton("Import", this);
	m_import->setObjectName("primary");
	m_import->setDefault(true);
	m_import->setEnabled(false);
	buttons->addWidget(m_cancel);
	buttons->addWidget(m_import);
	m_outer->addLayout(buttons);
	connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
	connect(m_import, &QPushButton::clicked, this, &ImportDialog::importRequested);
}

// The three columns, from the bake's description of the scene. Until the bake is in (m_pending)
// they show the remembered choices with empty lists, greyed out; setMeta rebuilds them.
void ImportDialog::buildBody() {
	auto* body = new QWidget(this);
	body->setEnabled(!m_pending);
	if (m_pending) {
		// Sisir's stylesheet has no disabled look, so waiting sections are dimmed as a whole.
		auto* dim = new QGraphicsOpacityEffect(body);
		dim->setOpacity(0.4);
		body->setGraphicsEffect(dim);
	}
	m_outer->replaceWidget(m_body, body);
	delete m_body;
	m_body = body;
	auto* outer = new QVBoxLayout(body);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);

	// Three columns, landscape like most DCC tools: the scene's surfaces and how to import them on
	// the left, the hair in the middle, guides and cards on the right. Lists take the spare height.
	auto* columns = new QHBoxLayout();
	columns->setSpacing(8);
	outer->addLayout(columns, 1);
	auto* left = new QVBoxLayout();
	auto* middle = new QVBoxLayout();
	auto* right = new QVBoxLayout();
	for (QVBoxLayout* c : { left, middle, right }) {
		c->setSpacing(6);
		columns->addLayout(c, 1);
	}
	auto section = [&](QVBoxLayout* column, const QString& name, int stretch) {
		QWidget* content = nullptr;
		QGroupBox* box = makeCollapsibleBox(name, nullptr, &content);
		auto* v = new QVBoxLayout(content);
		v->setContentsMargins(4, 4, 4, 4);
		v->setSpacing(4);
		column->addWidget(box, stretch);
		return v;
	};
	auto formIn = [](QVBoxLayout* v) {
		auto* f = new QFormLayout();
		f->setContentsMargins(0, 0, 0, 0);
		f->setLabelAlignment(Qt::AlignLeft);
		v->addLayout(f);
		return f;
	};
	auto check = [&](QFormLayout* form, const QString& label, bool value, bool def, const char* tipId) {
		auto* box = new QCheckBox(this);
		box->setChecked(value);
		tip(box, tipId);
		form->addRow(label, UiReset::check(box, def));
		return box;
	};
	const ImportOptions defaults;
	const QJsonObject mesh = m_meta.value("mesh").toObject();
	const QJsonObject ref = m_meta.value("refMesh").toObject();
	const bool hasMesh = m_pending || mesh.value("present").toBool();
	const bool hasRef = m_pending || ref.value("present").toBool();

	// --- Left: scene and options -------------------------------------------------------------
	QFormLayout* form = formIn(section(left, "Scene", 0));
	// A default that the scene cannot have (no reference mesh, no guides...) is "off", so no reset
	// arrow claims a changed value on a row that is only unavailable.
	m_mesh = check(form, "Mesh", m_options.mesh && hasMesh, defaults.mesh && hasMesh, importTooltipId("mesh"));
	m_mesh->setEnabled(hasMesh);
	m_refMesh = check(form, "Reference Mesh", m_options.refMesh && hasRef, defaults.refMesh && hasRef, importTooltipId("refMesh"));
	m_refMesh->setEnabled(hasRef);
	m_materials = check(form, "Materials && Textures", m_options.materials, defaults.materials, importTooltipId("materials"));

	form = formIn(section(left, "Options", 0));
	// What one scene unit stands for, in the words of Sisir's own unit popups.
	const QStringList units = { "mm", "cm", "m" };
	m_unit = new NoWheelComboBox();
	m_unit->addItems({ "Millimeters (mm)", "Centimeters (cm)", "Meters (m)" });
	const int unitIndex = (int)units.indexOf(m_options.resolvedUnit());
	const int defaultUnitIndex = std::max((int)units.indexOf(host().defaultUnit), 0);
	m_unit->setCurrentIndex(unitIndex >= 0 ? unitIndex : defaultUnitIndex);
	tip(m_unit, importTooltipId("unit"));
	form->addRow("Units", UiReset::combo(m_unit, defaultUnitIndex));
	m_textureFolder = makeTextField(m_options.textureFolder);
	m_textureFolder->setPlaceholderText(host().textureFolderPlaceholder);
	tip(m_textureFolder, importTooltipId("textureFolder"));
	form->addRow("Texture Folder", browseRow(m_textureFolder, this, true));
	left->addStretch(1);

	// --- Middle: hair ------------------------------------------------------------------------
	const QJsonArray hairSets = m_meta.value("hairSets").toArray();
	QVBoxLayout* hairBox = section(middle, "Hair", 1);
	form = formIn(hairBox);
	const bool hasHair = m_pending || !hairSets.isEmpty();
	m_hair = check(form, "Import Hair", m_options.hair && hasHair, defaults.hair && hasHair, importTooltipId("hair"));
	m_hair->setEnabled(hasHair);
	m_hairList = makeList(hairSets, "strands", "strands");
	tip(m_hairList, importTooltipId("hairSets"));
	hairBox->addWidget(m_hairList, 1);
	form = formIn(hairBox);
	m_displayPercent = makeFloatSlider(0.0, 100.0, 1.0, 0, m_options.displayPercent,
	                                   [this](double v) { m_displayValue = v; }, kDefaultDisplayPercent);
	tip(m_displayPercent, importTooltipId("displayPercent"));
	form->addRow("Display %", m_displayPercent);

	// --- Right: guides and cards -------------------------------------------------------------
	const QJsonArray guideLayers = m_meta.value("guideLayers").toArray();
	QVBoxLayout* guidesBox = section(right, "Guides", 1);
	form = formIn(guidesBox);
	const bool hasGuides = m_pending || !guideLayers.isEmpty();
	m_guides = check(form, "Import Guides", m_options.guides && hasGuides, defaults.guides && hasGuides, importTooltipId("guides"));
	m_guides->setEnabled(hasGuides);
	m_guideList = makeList(guideLayers, "curves", "curves");
	tip(m_guideList, importTooltipId("guideLayers"));
	guidesBox->addWidget(m_guideList, 1);
	form = formIn(guidesBox);
	m_guideDegree = new NoWheelComboBox();
	m_guideDegree->addItems({ "Linear", "Cubic" });
	m_guideDegree->setCurrentIndex(m_options.guideDegree == 3 ? 1 : 0);
	tip(m_guideDegree, importTooltipId("guideDegree"));
	form->addRow("Curve Degree", UiReset::combo(m_guideDegree, 0));

	const QJsonArray cardLayers = m_meta.value("cardLayers").toArray();
	QVBoxLayout* cardsBox = section(right, "Cards", 1);
	form = formIn(cardsBox);
	const bool hasCards = m_pending || !cardLayers.isEmpty();
	m_cards = check(form, "Import Cards", m_options.cards && hasCards, defaults.cards && hasCards, importTooltipId("cards"));
	m_cards->setEnabled(hasCards);
	m_cardList = makeList(cardLayers, m_options.smoothCards ? "facesSmooth" : "facesRaw", "faces");
	tip(m_cardList, importTooltipId("cardLayers"));
	cardsBox->addWidget(m_cardList, 1);
	form = formIn(cardsBox);
	m_smoothCards = check(form, "Smooth Cards", m_options.smoothCards, defaults.smoothCards, importTooltipId("smoothCards"));

	// Each list follows its master switch.
	auto follow = [](QCheckBox* master, QWidget* dependent) {
		dependent->setEnabled(master->isChecked() && master->isEnabled());
		QObject::connect(master, &QCheckBox::toggled, dependent, [=](bool on) { dependent->setEnabled(on); });
	};
	follow(m_guides, m_guideList);
	follow(m_guides, m_guideDegree);
	follow(m_hair, m_hairList);
	follow(m_hair, m_displayPercent);
	follow(m_cards, m_cardList);
	follow(m_cards, m_smoothCards);
	UiReset::refreshSections(this);
	// Built after the dialog is up (setMeta): a new child of a visible window stays hidden until shown.
	m_body->show();
}

ImportOptions ImportDialog::options() const {
	ImportOptions o = m_options;
	o.mesh = m_mesh->isChecked() && m_mesh->isEnabled();
	o.refMesh = m_refMesh->isChecked() && m_refMesh->isEnabled();
	o.materials = m_materials->isChecked();
	o.guides = m_guides->isChecked() && m_guides->isEnabled();
	o.hair = m_hair->isChecked() && m_hair->isEnabled();
	o.cards = m_cards->isChecked() && m_cards->isEnabled();
	o.guideLayers = checkedNames(m_guideList, nullptr);
	o.hairSets = checkedNames(m_hairList, nullptr);
	o.cardLayers = checkedNames(m_cardList, nullptr);
	// An explicit empty list means "none" for that kind.
	if (o.guideLayers.isEmpty()) o.guides = false;
	if (o.hairSets.isEmpty()) o.hair = false;
	if (o.cardLayers.isEmpty()) o.cards = false;
	o.guideDegree = m_guideDegree->currentIndex() == 1 ? 3 : 1;
	o.displayPercent = m_displayValue;
	o.smoothCards = m_smoothCards->isChecked();
	o.unit = QStringList{ "mm", "cm", "m" }.value(m_unit->currentIndex(), host().defaultUnit);
	o.textureFolder = m_textureFolder->text().trimmed();
	return o;
}

} // namespace SisirDcc
