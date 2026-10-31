#include "Baker.h"

#include "HostInfo.h"
#include "SirBakeFormat.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include <memory>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace SisirDcc {

namespace {

constexpr int kStallTimeoutMs = 10 * 60 * 1000; // no output for this long: the bake is stuck
constexpr int kCacheKeep = 12;                  // bakes kept in the cache

QString registryOpenCommand() {
	// HKCU\Software\Classes\Sisir.Scene\shell\open\command = "C:\...\Sisir.exe" "%1"
	for (HKEY root : { HKEY_CURRENT_USER, HKEY_CLASSES_ROOT }) {
		const wchar_t* sub = root == HKEY_CURRENT_USER ? L"Software\\Classes\\Sisir.Scene\\shell\\open\\command"
		                                               : L"Sisir.Scene\\shell\\open\\command";
		HKEY key = nullptr;
		if (RegOpenKeyExW(root, sub, 0, KEY_READ, &key) != ERROR_SUCCESS) continue;
		wchar_t buffer[2048] = {};
		DWORD size = sizeof(buffer) - sizeof(wchar_t);
		DWORD type = 0;
		const LONG r = RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
		RegCloseKey(key);
		if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) continue;
		const QString cmd = QString::fromWCharArray(buffer).trimmed();
		QString exe;
		if (cmd.startsWith('"')) exe = cmd.mid(1, cmd.indexOf('"', 1) - 1);
		else exe = cmd.section(' ', 0, 0);
		if (QFileInfo(exe).isFile()) return QDir::toNativeSeparators(exe);
	}
	return {};
}

QString cacheDir() {
	QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
	if (base.isEmpty()) base = QDir::tempPath();
	return QDir(base).filePath("Sisir/BakeCache");
}

QString cacheKey(const QFileInfo& sir, const QFileInfo& exe) {
	QCryptographicHash h(QCryptographicHash::Sha1);
	h.addData(sir.absoluteFilePath().toLower().toUtf8());
	h.addData(QByteArray::number(sir.size()));
	h.addData(QByteArray::number(sir.lastModified().toMSecsSinceEpoch()));
	h.addData(exe.absoluteFilePath().toLower().toUtf8());
	h.addData(QByteArray::number(exe.size()));
	h.addData(QByteArray::number(exe.lastModified().toMSecsSinceEpoch()));
	h.addData(QByteArray::number(SirBake::kFormatVersion));
	return QString::fromLatin1(h.result().toHex().left(24));
}

void pruneCache(const QDir& dir) {
	QFileInfoList bakes = dir.entryInfoList({ "*.sirbake" }, QDir::Files, QDir::Time);
	for (int i = kCacheKeep; i < bakes.size(); i++) QFile::remove(bakes[i].absoluteFilePath());
	for (const QFileInfo& tmp : dir.entryInfoList({ "*.tmp" }, QDir::Files)) {
		if (tmp.lastModified().secsTo(QDateTime::currentDateTime()) > 3600) QFile::remove(tmp.absoluteFilePath());
	}
}

} // namespace

QString Baker::findSisir(const QString& explicitPath) {
	if (!explicitPath.isEmpty() && QFileInfo(explicitPath).isFile()) return QDir::toNativeSeparators(explicitPath);
	// The Sisir started last wins: a user who unzips a newer Sisir next to the old one bakes with
	// the newer. The remembered one only when that cannot bake (an older Sisir started last).
	const QString latest = registryOpenCommand();
	if (!latest.isEmpty() && canBake(latest, nullptr)) return latest;
	const QString remembered = QSettings("Sisir", host().settingsApplication).value("sisirExecutable").toString();
	if (!remembered.isEmpty() && QFileInfo(remembered).isFile()) return remembered;
	return latest;
}

void Baker::rememberSisir(const QString& exe) {
	if (!exe.isEmpty()) QSettings("Sisir", host().settingsApplication).setValue("sisirExecutable", exe);
}

bool Baker::canBake(const QString& exe, QString* why) {
	QFile f(exe);
	if (!f.open(QIODevice::ReadOnly)) {
		if (why) *why = "cannot read " + exe;
		return false;
	}
	// The bake's usage text is compiled into every Sisir that has --bake (BakeExport.cpp).
	const QByteArray marker("usage: Sisir --bake");
	QByteArray previous;
	while (!f.atEnd()) {
		QByteArray block = f.read(8 << 20);
		const QByteArray window = previous + block;
		if (window.contains(marker)) return true;
		previous = block.right(marker.size());
	}
	if (why) *why = "this Sisir is too old to import from: update Sisir to 1.3 or later";
	return false;
}

namespace {

// Where a bake goes, and whether the cache already holds it.
struct BakePlan {
	QString bakeFile;
	QString stamp; // in the assets folder: which bake its meshes and textures belong to
	QString key;
	bool cached = false;
};

bool planBake(const QString& sirFile, const QString& assetsDir, const QString& sisirExe, BakePlan& plan, BakeResult& result) {
	result.assetsDir = assetsDir;
	const QFileInfo sir(sirFile), exe(sisirExe);
	if (!sir.isFile()) {
		result.error = "file not found: " + sirFile;
		return false;
	}
	if (!exe.isFile()) {
		result.error = "Sisir was not found. Set its location in the import options.";
		return false;
	}
	QString why;
	if (!Baker::canBake(sisirExe, &why)) {
		result.error = why;
		return false;
	}
	QDir cache(cacheDir());
	cache.mkpath(".");
	plan.key = cacheKey(sir, exe);
	plan.bakeFile = cache.filePath(plan.key + ".sirbake");
	plan.stamp = QDir(assetsDir).filePath(".sisir_bake_key");
	QFile f(plan.stamp);
	plan.cached = QFileInfo(plan.bakeFile).isFile() && f.open(QIODevice::ReadOnly) && f.readAll().trimmed() == plan.key.toLatin1();
	if (plan.cached) {
		result.ok = true;
		result.bakeFile = plan.bakeFile;
	}
	return true;
}

void startBakeProcess(QProcess& process, const QString& sirFile, const QString& assetsDir, const QString& sisirExe,
                      const BakePlan& plan) {
	QDir().mkpath(assetsDir);
	process.setProgram(sisirExe);
	process.setArguments({ "--bake", QDir::toNativeSeparators(QFileInfo(sirFile).absoluteFilePath()),
	                       QDir::toNativeSeparators(plan.bakeFile), "--assets", QDir::toNativeSeparators(assetsDir),
	                       "--progress" });
	process.setProcessChannelMode(QProcess::SeparateChannels);
	process.start();
}

// Complete "PROGRESS <fraction> <label>" lines from Sisir's output. False when `progress` cancels.
bool parseProgress(QByteArray& pending, const Baker::Progress& progress) {
	int nl;
	while ((nl = pending.indexOf('\n')) >= 0) {
		const QString line = QString::fromUtf8(pending.left(nl)).trimmed();
		pending.remove(0, nl + 1);
		if (line.startsWith("PROGRESS ")) {
			const QString rest = line.mid(9);
			const int sp = rest.indexOf(' ');
			const float f = rest.left(sp).toFloat();
			if (progress && !progress(f, sp > 0 ? rest.mid(sp + 1) : QString())) return false;
		}
	}
	return true;
}

// The finished process as a result: Sisir's own error line when it failed, else the bake, stamped.
BakeResult finishBake(const QProcess& process, const QString& errorText, const BakePlan& plan, const QString& assetsDir) {
	BakeResult result;
	result.assetsDir = assetsDir;
	const int code = process.exitCode();
	if (process.exitStatus() != QProcess::NormalExit || code != SirBake::kExitOk) {
		QString message;
		for (const QString& line : errorText.split('\n', Qt::SkipEmptyParts)) {
			if (line.startsWith("ERROR ")) message = line.mid(6).trimmed();
		}
		if (message.isEmpty()) message = QString("Sisir could not prepare the scene (exit code %1)").arg(code);
		result.error = message;
		return result;
	}
	QFile f(plan.stamp);
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(plan.key.toLatin1());
	pruneCache(QDir(cacheDir()));
	result.ok = true;
	result.bakeFile = plan.bakeFile;
	return result;
}

} // namespace

BakeResult Baker::bake(const QString& sirFile, const QString& assetsDir, const QString& sisirExe, const Progress& progress) {
	BakeResult result;
	BakePlan plan;
	if (!planBake(sirFile, assetsDir, sisirExe, plan, result)) return result;
	if (plan.cached) {
		if (progress) progress(1.0f, "Using the cached bake");
		return result;
	}

	QProcess process;
	startBakeProcess(process, sirFile, assetsDir, sisirExe, plan);
	if (!process.waitForStarted(30000)) {
		result.error = "Sisir could not be started: " + process.errorString();
		return result;
	}

	QString errorText;
	QByteArray pending;
	QElapsedTimer sinceOutput;
	sinceOutput.start();
	auto drain = [&] {
		const QByteArray out = process.readAllStandardOutput();
		if (!out.isEmpty()) sinceOutput.restart();
		pending += out;
		if (!parseProgress(pending, progress)) return false;
		const QByteArray err = process.readAllStandardError();
		if (!err.isEmpty()) {
			errorText += QString::fromUtf8(err);
			sinceOutput.restart();
		}
		return true;
	};
	while (process.state() != QProcess::NotRunning) {
		process.waitForReadyRead(50);
		if (!drain()) {
			process.kill();
			process.waitForFinished(5000);
			QFile::remove(plan.bakeFile);
			result.cancelled = true;
			result.error = "Import cancelled";
			return result;
		}
		if (sinceOutput.elapsed() > kStallTimeoutMs) {
			process.kill();
			process.waitForFinished(5000);
			result.error = "Sisir stopped responding while preparing the scene";
			return result;
		}
		if (progress && !progress(-1.0f, QString())) { // keep the UI alive between lines
			process.kill();
			process.waitForFinished(5000);
			result.cancelled = true;
			result.error = "Import cancelled";
			return result;
		}
	}
	drain();
	return finishBake(process, errorText, plan, assetsDir);
}

// --- BakeJob ----------------------------------------------------------------------------------

BakeJob::BakeJob(QObject* parent) : QObject(parent) {}

BakeJob::~BakeJob() { cancel(); }

void BakeJob::start(const QString& sirFile, const QString& assetsDir, const QString& sisirExe) {
	m_sirFile = sirFile;
	m_assetsDir = assetsDir;
	BakeResult result;
	auto plan = std::make_shared<BakePlan>();
	if (!planBake(sirFile, assetsDir, sisirExe, *plan, result) || plan->cached) {
		// Cached (or impossible): finished as soon as the caller is back in the event loop, so the
		// dialog's first paint is never held up and every outcome arrives the same way.
		QTimer::singleShot(0, this, [this, result] { finish(result); });
		return;
	}
	m_process = new QProcess(this);
	m_stall = new QTimer(this);
	m_stall->setSingleShot(true);
	m_stall->setInterval(kStallTimeoutMs);
	connect(m_stall, &QTimer::timeout, this, [this] {
		BakeResult r;
		r.assetsDir = m_assetsDir;
		r.error = "Sisir stopped responding while preparing the scene";
		killProcess();
		finish(r);
	});
	connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
		m_stall->start();
		m_pending += m_process->readAllStandardOutput();
		parseProgress(m_pending, [this](float f, const QString& label) {
			Q_EMIT progress(f, label);
			return true;
		});
	});
	connect(m_process, &QProcess::readyReadStandardError, this, [this] {
		m_stall->start();
		m_errorText += QString::fromUtf8(m_process->readAllStandardError());
	});
	connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart || m_done) return;
		BakeResult r;
		r.assetsDir = m_assetsDir;
		r.error = "Sisir could not be started: " + m_process->errorString();
		finish(r);
	});
	// The overload by name: Qt 5 (Maya 2022-2024, 3ds Max 2022-2024) also has finished(int).
	connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, plan] {
		if (m_done) return;
		m_pending += m_process->readAllStandardOutput();
		parseProgress(m_pending, [this](float f, const QString& label) {
			Q_EMIT progress(f, label);
			return true;
		});
		m_errorText += QString::fromUtf8(m_process->readAllStandardError());
		finish(finishBake(*m_process, m_errorText, *plan, m_assetsDir));
	});
	m_planBakeFile = plan->bakeFile;
	startBakeProcess(*m_process, sirFile, assetsDir, sisirExe, *plan);
	m_stall->start();
}

void BakeJob::cancel() {
	if (m_done) return;
	m_done = true;
	if (!m_process) return;
	killProcess();
	QFile::remove(m_planBakeFile);
}

void BakeJob::killProcess() {
	if (m_stall) m_stall->stop();
	if (!m_process || m_process->state() == QProcess::NotRunning) return;
	// Disconnected first: the kill must not report itself as a failed bake.
	m_process->disconnect(this);
	m_process->kill();
	m_process->waitForFinished(5000);
}

void BakeJob::finish(const BakeResult& result) {
	if (m_done) return;
	m_done = true;
	if (m_stall) m_stall->stop();
	Q_EMIT finished(result);
}

} // namespace SisirDcc
