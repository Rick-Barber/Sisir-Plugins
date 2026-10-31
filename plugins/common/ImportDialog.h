#pragma once

// The Sisir import options dialog, dressed exactly like Sisir (ui_style/): what to import and how.
// It opens as soon as a file is picked and shows the bake's progress while its sections wait,
// greyed out, for the scene's description (setMeta); after Import it shows the build's progress
// until the build closes it (setBuilding, showBuildProgress, finishBuilding).

#include "ImportOptions.h"

#include <QDialog>
#include <QJsonObject>
#include <QPointer>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

namespace SisirDcc {

// Applies Sisir's palette, stylesheet and font to a top-level widget (never to the host's own UI).
void applySisirLook(QWidget* w);

class ImportDialog : public QDialog {
	Q_OBJECT
public:
	ImportDialog(const ImportOptions& options, const QString& sourceFile, QWidget* parent);
	ImportOptions options() const;

	// The open dialog whose Import is being built, if any: where the build reports progress.
	static ImportDialog* active();

	void setMeta(const QJsonObject& meta); // the bake is in: the sections fill and Import enables
	void setProgress(float fraction, const QString& label); // fraction < 0 keeps the bar
	void setBuilding();                      // everything locked, progress for the build
	void showBuildProgress(float fraction, const QString& label); // painted at once
	void finishBuilding();                   // the build is over: close
	bool building() const { return m_building; }

	void reject() override;

Q_SIGNALS:
	void importRequested();

protected:
	void closeEvent(QCloseEvent* event) override;
	void paintEvent(QPaintEvent* event) override;

private:
	void buildFrame(const QString& sourceFile);
	void buildBody();
	QListWidget* makeList(const QJsonArray& entries, const QString& countKey, const QString& countLabel);
	static QStringList checkedNames(const QListWidget* list, bool* allVisibleOnly);

	static QPointer<ImportDialog> s_active;

	QJsonObject m_meta;
	ImportOptions m_options;
	bool m_pending = true;
	bool m_building = false;
	bool m_revealed = false;
	QVBoxLayout* m_outer = nullptr;
	QWidget* m_body = nullptr;
	QLabel* m_version = nullptr;
	QProgressBar* m_progress = nullptr;
	QLabel* m_status = nullptr;
	QPushButton* m_cancel = nullptr;
	QPushButton* m_import = nullptr;
	QCheckBox* m_mesh = nullptr;
	QCheckBox* m_refMesh = nullptr;
	QCheckBox* m_materials = nullptr;
	QCheckBox* m_guides = nullptr;
	QCheckBox* m_hair = nullptr;
	QCheckBox* m_cards = nullptr;
	QListWidget* m_guideList = nullptr;
	QListWidget* m_hairList = nullptr;
	QListWidget* m_cardList = nullptr;
	QWidget* m_displayPercent = nullptr;
	QCheckBox* m_smoothCards = nullptr;
	QComboBox* m_unit = nullptr;
	QComboBox* m_guideDegree = nullptr;
	QLineEdit* m_textureFolder = nullptr;
	double m_displayValue = kDefaultDisplayPercent;
};

} // namespace SisirDcc
