#include "AbcExportDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace AbcExportDialog {

// Unreal trims hair strands past 255 control points on import (UE 5.3+), so an ABC that is
// perfectly valid everywhere else comes into UE truncated. Purely advisory: nothing is clamped
// or resampled, because Maya, Blender and Houdini have no such limit and a user targeting them
// wants the resolution they asked for. Returns whether to go ahead with the export.
//
// Modal rather than a toast because it arrives at the moment of no return -- once the file is
// written the user has no signal that anything is wrong until UE mangles the groom. It is also
// the one dialog here that a user may see on every export of a deliberately dense groom, hence
// the opt-out.
constexpr const char* kUnrealWarningSettingsKey = "qt/dontWarnUnrealPointLimit";

bool confirmUnrealPointLimit(QWidget* parent, int maxPoints) {
	QSettings settings("Sisir", "Sisir");
	if (settings.value(kUnrealWarningSettingsKey, false).toBool()) return true;

	QDialog dlg(parent);
	dlg.setWindowTitle("Unreal Engine Point Limit");
	dlg.setWindowIcon(parent ? parent->windowIcon() : QApplication::windowIcon());
	dlg.setModal(true);
	dlg.setFixedWidth(380);

	QVBoxLayout* layout = new QVBoxLayout(&dlg);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(8);

	QLabel* label = new QLabel(
		QString("This groom has %1 control points per strand.\n\n"
		        "If you are exporting to Unreal Engine:\n"
		        "UE supports a maximum of %2 points per strand. Above this limit, the hair will "
		        "be trimmed or look incorrect.\n"
		        "To stay within the limit, lower the Steps under Hair Resolution, or increase "
		        "Distance when using Distance Based resolution.\n\n"
		        "If you are exporting to Maya, Blender, Houdini, or other DCCs, you can ignore "
		        "this warning.")
			.arg(maxPoints)
			.arg(ExportAbc::kUnrealMaxPointsPerStrand),
		&dlg);
	label->setWordWrap(true);
	layout->addWidget(label);

	auto* dontAsk = new QCheckBox("Don't show this again", &dlg);
	layout->addWidget(dontAsk);

	// Rejecting (Esc / title-bar X) must be the safe answer.
	bool confirmed = false;

	QHBoxLayout* btnRow = new QHBoxLayout;
	auto* cancelBtn = new QPushButton("Cancel", &dlg);
	auto* okBtn = new QPushButton("Continue Export", &dlg);
	cancelBtn->setDefault(true);
	btnRow->addWidget(cancelBtn);
	btnRow->addWidget(okBtn);
	layout->addLayout(btnRow);
	QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
	QObject::connect(okBtn, &QPushButton::clicked, &dlg, [&]() {
		confirmed = true;
		dlg.accept();
	});

	dlg.exec();
	// Only remembered on a deliberate "Continue Export" -- ticking the box and then cancelling
	// reads as backing out, not as silencing the warning forever.
	if (confirmed && dontAsk->isChecked()) {
		settings.setValue(kUnrealWarningSettingsKey, true);
		settings.sync();
	}
	return confirmed;
}

// The ABC exporter needs more than a unit factor: which groom attributes to write, and whether
// to bake Unreal's axis conversion. Sisir's showUnitDialog stays untouched (nine other callers),
// so the unit combo is repeated here rather than shared.
//
// Returns false when the user cancels; on accept, `outScale` is the m -> target-unit factor and
// `io` carries the chosen options in and the user's answer out.
constexpr const char* kAbcExportSettingsPrefix = "qt/abcExport/";

bool show(QWidget* parent, float& outScale, ExportAbc::Options& io) {
	QSettings settings("Sisir", "Sisir");
	auto settingsKey = [](const char* name) { return QString(kAbcExportSettingsPrefix) + name; };
	auto storedBool = [&](const char* name, bool fallback) {
		return settings.value(settingsKey(name), fallback).toBool();
	};

	QDialog dlg(parent);
	dlg.setWindowTitle("Export Hair (ABC)");
	dlg.setWindowIcon(parent ? parent->windowIcon() : QApplication::windowIcon());
	dlg.setModal(true);
	dlg.setFixedWidth(360);

	QVBoxLayout* layout = new QVBoxLayout(&dlg);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(8);

	layout->addWidget(new QLabel("Output units:", &dlg));
	QComboBox* combo = new QComboBox(&dlg);
	combo->addItem("Millimeters (mm)");
	combo->addItem("Centimeters (cm)");
	combo->addItem("Meters (m)");
	combo->setCurrentIndex(settings.value(settingsKey("units"), 1).toInt());
	layout->addWidget(combo);

	QGroupBox* dataBox = new QGroupBox("Exported Data", &dlg);
	QGridLayout* dataGrid = new QGridLayout(dataBox);
	dataGrid->setContentsMargins(12, 8, 12, 8);
	dataGrid->setHorizontalSpacing(12);
	dataGrid->setVerticalSpacing(6);

	auto* rootUvBox = new QCheckBox("Root UV", dataBox);
	auto* thicknessBox = new QCheckBox("Thickness", dataBox);
	auto* clumpIdBox = new QCheckBox("Clump ID", dataBox);
	auto* colorBox = new QCheckBox("Color", dataBox);
	auto* roughnessBox = new QCheckBox("Roughness", dataBox);
	auto* aoBox = new QCheckBox("AO", dataBox);

	rootUvBox->setChecked(storedBool("rootUv", io.rootUv));
	thicknessBox->setChecked(storedBool("thickness", io.thickness));
	clumpIdBox->setChecked(storedBool("clumpId", io.clumpId));
	colorBox->setChecked(storedBool("color", io.color));
	roughnessBox->setChecked(storedBool("roughness", io.roughness));
	aoBox->setChecked(storedBool("ao", io.ao));

	rootUvBox->setToolTip("groom_root_uv: the mesh UV at each strand root, used by Unreal to bind "
	                      "the groom to the skeletal mesh.");
	thicknessBox->setToolTip("Per-point strand width, from the Hair Thickness profile.");
	clumpIdBox->setToolTip("groom_clumpid: the clump each strand belongs to. Unclumped strands are "
	                       "written as their own single-strand clump.");
	colorBox->setToolTip("groom_color: Sisir stores no per-strand color, so the hair set's color is "
	                     "written to every control point as a starting value.");
	roughnessBox->setToolTip("groom_roughness: taken from the render material assigned to the hair "
	                         "set and written to every control point.");
	aoBox->setToolTip("groom_ao: taken from the render material's hair AO and written to every "
	                  "control point.");

	dataGrid->addWidget(rootUvBox, 0, 0);
	dataGrid->addWidget(thicknessBox, 0, 1);
	dataGrid->addWidget(clumpIdBox, 0, 2);
	dataGrid->addWidget(colorBox, 1, 0);
	dataGrid->addWidget(roughnessBox, 1, 1);
	dataGrid->addWidget(aoBox, 1, 2);
	layout->addWidget(dataBox);

	QGroupBox* orientBox = new QGroupBox("Orientation", &dlg);
	QVBoxLayout* orientLayout = new QVBoxLayout(orientBox);
	orientLayout->setContentsMargins(12, 8, 12, 8);
	orientLayout->setSpacing(6);
	auto* unrealOrientBox = new QCheckBox("Unreal Engine orientation", orientBox);
	unrealOrientBox->setChecked(storedBool("unrealOrientation", io.unrealOrientation));
	unrealOrientBox->setToolTip("Bakes the conversion from Sisir's Y-up space into Unreal's Z-up "
	                            "space (Rotation -90, 0, 0 with Scale 1, 1, -1).\n"
	                            "Leave Unreal's groom import Conversion settings at their defaults.");
	orientLayout->addWidget(unrealOrientBox);
	layout->addWidget(orientBox);

	// Rejecting (Esc / title-bar X) must not export.
	bool confirmed = false;

	QHBoxLayout* btnRow = new QHBoxLayout;
	auto* cancelBtn = new QPushButton("Cancel", &dlg);
	auto* okBtn = new QPushButton("Export", &dlg);
	okBtn->setDefault(true);
	btnRow->addWidget(cancelBtn);
	btnRow->addWidget(okBtn);
	layout->addLayout(btnRow);
	QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
	QObject::connect(okBtn, &QPushButton::clicked, &dlg, [&]() {
		confirmed = true;
		dlg.accept();
	});

	dlg.exec();
	if (!confirmed) return false;

	const int idx = combo->currentIndex();
	outScale = (idx == 0) ? 1000.0f : (idx == 1) ? 100.0f : 1.0f; // m -> mm / cm / m
	io.rootUv = rootUvBox->isChecked();
	io.thickness = thicknessBox->isChecked();
	io.clumpId = clumpIdBox->isChecked();
	io.color = colorBox->isChecked();
	io.roughness = roughnessBox->isChecked();
	io.ao = aoBox->isChecked();
	io.unrealOrientation = unrealOrientBox->isChecked();

	// Only persisted on a deliberate Export, matching confirmUnrealPointLimit.
	settings.setValue(settingsKey("units"), idx);
	settings.setValue(settingsKey("rootUv"), io.rootUv);
	settings.setValue(settingsKey("thickness"), io.thickness);
	settings.setValue(settingsKey("clumpId"), io.clumpId);
	settings.setValue(settingsKey("color"), io.color);
	settings.setValue(settingsKey("roughness"), io.roughness);
	settings.setValue(settingsKey("ao"), io.ao);
	settings.setValue(settingsKey("unrealOrientation"), io.unrealOrientation);
	settings.sync();
	return true;
}

void loadRemembered(float& outScale, ExportAbc::Options& io) {
	QSettings settings("Sisir", "Sisir");
	auto settingsKey = [](const char* name) { return QString(kAbcExportSettingsPrefix) + name; };
	auto storedBool = [&](const char* name, bool fallback) {
		return settings.value(settingsKey(name), fallback).toBool();
	};
	const int idx = settings.value(settingsKey("units"), 1).toInt();
	outScale = (idx == 0) ? 1000.0f : (idx == 2) ? 1.0f : 100.0f; // m -> mm / cm / m
	io.rootUv = storedBool("rootUv", io.rootUv);
	io.thickness = storedBool("thickness", io.thickness);
	io.clumpId = storedBool("clumpId", io.clumpId);
	io.color = storedBool("color", io.color);
	io.roughness = storedBool("roughness", io.roughness);
	io.ao = storedBool("ao", io.ao);
	io.unrealOrientation = storedBool("unrealOrientation", io.unrealOrientation);
}

} // namespace AbcExportDialog
