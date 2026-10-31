#include "MaxUtil.h"

#include <maxscript/maxscript.h>
#include <maxscript/util/listener.h>
#include <units.h>

#include <QDir>
#include <QFileInfo>
#include <QTimer>

namespace SisirMax {

namespace {
HINSTANCE g_instance = nullptr;
}

void setHInstance(HINSTANCE instance) { g_instance = instance; }

HINSTANCE hInstance() { return g_instance; }

QString pluginPath() {
	wchar_t path[MAX_PATH * 4] = {};
	const DWORD n = GetModuleFileNameW(g_instance, path, (DWORD)(sizeof(path) / sizeof(path[0])));
	return n ? QDir::fromNativeSeparators(QString::fromWCharArray(path, (int)n)) : QString();
}

QString contentsDir() { return QFileInfo(QFileInfo(pluginPath()).absolutePath()).absolutePath(); }

QString msQuote(const QString& s) {
	QString out = s;
	out.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n");
	return "\"" + out + "\"";
}

bool runMaxScript(const QString& script, FPValue* result) {
	FPValue local;
	const BOOL ok = ExecuteMAXScriptScript(toM(script).data(), MAXScript::ScriptSource::NonEmbedded, TRUE,
	                                       result ? result : &local, TRUE);
	return ok != FALSE;
}

bool runMaxScriptBool(const QString& script) {
	FPValue v;
	if (!runMaxScript(script, &v)) return false;
	return v.type == TYPE_BOOL && v.i != 0;
}

bool interactive() {
	Interface* ip = GetCOREInterface();
	return ip && !ip->GetQuietMode() && IsWindowVisible(ip->GetMAXHWnd());
}

namespace {
void toListener(const QString& prefix, const QString& text) {
	const QString line = prefix + text + "\n";
	the_listener->edit_stream->wputs(toM(line).data());
	the_listener->edit_stream->flush();
}
} // namespace

void logInfo(const QString& text) { toListener("Sisir: ", text); }
void logWarning(const QString& text) { toListener("Sisir warning: ", text); }
void logError(const QString& text) { toListener("Sisir error: ", text); }

void showErrorLater(const QString& title, const QString& text) {
	logError(text);
	if (!interactive()) return;
	// After the current command or import returns, as a plain message box of Max's own.
	QTimer::singleShot(0, [title, text] {
		MessageBoxW(GetCOREInterface()->GetMAXHWnd(), toM(text).data(), toM(title).data(), MB_OK | MB_ICONWARNING);
	});
}

double systemUnitsPerMetre() {
	const double metresPerUnit = GetSystemUnitScale(UNITS_METERS);
	return metresPerUnit > 1e-12 ? 1.0 / metresPerUnit : 100.0;
}

QString legalName(const QString& name, const QString& fallback) {
	QString out;
	for (const QChar c : name) out += (c.isLetterOrNumber() && c.unicode() < 128) || c == '_' ? c : QChar('_');
	while (out.contains("__")) out.replace("__", "_");
	if (out.isEmpty() || out == "_") out = fallback;
	return out;
}

} // namespace SisirMax
