#include "PlyExportDialog.h"

#include "HostInfo.h"
#include "ImportDialog.h"
#include "UiTooltips.h"

#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>

namespace SisirDcc {

namespace {

const QStringList kUnits = { "mm", "cm", "m" };
constexpr const char* kSettingsKey = "plyExportUnits";

} // namespace

QString rememberedPlyUnit() {
	const QString unit = QSettings("Sisir", host().settingsApplication).value(kSettingsKey).toString();
	return kUnits.contains(unit) ? unit : host().defaultUnit;
}

QString askPlyUnit(QWidget* parent) {
	QDialog dlg(parent);
	applySisirLook(&dlg);
	dlg.setWindowTitle("Export Guides (PLY)");
	dlg.setModal(true);
	dlg.setFixedWidth(360);

	auto* layout = new QVBoxLayout(&dlg);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(8);
	layout->addWidget(new QLabel("Output units:", &dlg));
	auto* combo = new QComboBox(&dlg);
	combo->addItems({ "Millimeters (mm)", "Centimeters (cm)", "Meters (m)" });
	combo->setCurrentIndex(std::max(0, (int)kUnits.indexOf(rememberedPlyUnit())));
	combo->setToolTip(UiTooltips::text("common.plyExport.unit"));
	layout->addWidget(combo);

	auto* buttons = new QHBoxLayout;
	auto* cancel = new QPushButton("Cancel", &dlg);
	auto* ok = new QPushButton("Export", &dlg);
	ok->setObjectName("primary");
	ok->setDefault(true);
	buttons->addWidget(cancel);
	buttons->addWidget(ok);
	layout->addLayout(buttons);
	QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
	QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
	if (dlg.exec() != QDialog::Accepted) return {};

	const QString unit = kUnits.value(combo->currentIndex(), host().defaultUnit);
	// Only remembered on a deliberate Export, as Export Hair (ABC) does.
	QSettings settings("Sisir", host().settingsApplication);
	settings.setValue(kSettingsKey, unit);
	settings.sync();
	return unit;
}

} // namespace SisirDcc
