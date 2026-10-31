#pragma once

// Runs `Sisir.exe --bake` for a .sir and hands back the .sirbake, with progress and Cancel.
//
// Finding Sisir: an explicit path first (the dialog's field / the -sisir flag), then the Sisir
// started last (the .sir file association Sisir writes on every launch), then the one remembered
// from last time or from installing the plug-in. A Sisir
// too old to bake would treat the arguments as a file to open and start its window, so the
// executable is checked for the bake entry point before it is run.
//
// Bakes are cached per (.sir, Sisir build): importing the same file twice runs Sisir once.

#include <QObject>
#include <QString>

class QProcess;
class QTimer;

#include <functional>

namespace SisirDcc {

struct BakeResult {
	bool ok = false;
	bool cancelled = false;
	QString bakeFile;   // the .sirbake
	QString assetsDir;  // meshes and textures it refers to
	QString error;
};

class Baker {
public:
	// fraction in [0, 1]; return false to cancel.
	using Progress = std::function<bool(float fraction, const QString& label)>;

	// Empty when none is found. `explicitPath` wins when it exists.
	static QString findSisir(const QString& explicitPath);
	// Whether this executable can bake (has the --bake entry point).
	static bool canBake(const QString& exe, QString* why);
	static void rememberSisir(const QString& exe);

	static BakeResult bake(const QString& sirFile, const QString& assetsDir, const QString& sisirExe,
	                       const Progress& progress);
};

// The same bake without blocking: Sisir runs while the host's event loop does, for the import dialog.
// `progress` follows Sisir's output; `finished` arrives exactly once, also for a cached bake
// (on the next event-loop turn) -- never after cancel() or destruction, which stop Sisir.
class BakeJob : public QObject {
	Q_OBJECT
public:
	explicit BakeJob(QObject* parent = nullptr);
	~BakeJob() override;
	void start(const QString& sirFile, const QString& assetsDir, const QString& sisirExe);
	void cancel();

Q_SIGNALS:
	void progress(float fraction, const QString& label);
	void finished(const SisirDcc::BakeResult& result);

private:
	void killProcess();
	void finish(const BakeResult& result);

	QProcess* m_process = nullptr;
	QTimer* m_stall = nullptr;
	QString m_sirFile, m_assetsDir, m_planBakeFile, m_errorText;
	QByteArray m_pending;
	bool m_done = false;
};

} // namespace SisirDcc
