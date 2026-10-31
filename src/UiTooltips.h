#pragma once

// Hover help for every control in the UI, in one place.
//
// A control does not carry its tooltip TEXT, only a stable dotted ID ("cards.shape.centerRidge"),
// and the text is looked up from the catalogue in UiTooltips.cpp at the moment a tooltip is about
// to appear. Two reasons:
//
//   - Translation. The IDs never derive from label text, so labels can be translated without
//     breaking a single tooltip, and a language only has to supply a table keyed by the same IDs
//     (setLanguageLookup). The dotted form is also what Qt's id-based translation (qtTrId) uses,
//     should the catalogue ever move into .ts files.
//   - Cost. bind() is one property write -- it never calls setToolTip, never touches layout, never
//     repaints -- so an inspector rebuild of a thousand widgets pays nothing for it, and nothing
//     about tooltips can ever cause a rebuild or a refresh.
//
// Resolution (install()): one application-wide event filter answers QEvent::ToolTip. It walks up
// from the hovered widget to the first one bound to an ID. Hovering a form ROW LABEL shows its
// field's tooltip, so only fields need binding. A widget with its own explicit toolTip() -- the
// few whose text depends on state (see text() with arguments) -- is left to Qt as usual.

#include <QString>
#include <QVariant>
#include <QWidget>

#include <functional>
#include <string_view>

class QApplication;
class QFormLayout;

namespace UiTooltips {

inline constexpr const char* kIdProperty = "tipId";

// Tags `w` with a catalogue ID and returns it, so it can wrap a constructor inline:
//   form->addRow("Gravity", UiTooltips::bind(gravity, "common.sim.gravity"));
// `id` must be a string literal (it is referenced, not copied).
template <class W>
W* bind(W* w, const char* id) {
	if (w && id) w->setProperty(kIdProperty, QByteArray::fromRawData(id, (qsizetype)std::char_traits<char>::length(id)));
	return w;
}

// The text for `id` in the active language, falling back to English; empty for an unknown ID.
QString text(std::string_view id);

// Catalogue text with %1.. filled in, for the few tooltips whose content depends on state
// (a count, a name). Set with setToolTip at build time, since the arguments are only known then.
template <class... Args>
QString text(std::string_view id, Args&&... args) {
	return text(id).arg(std::forward<Args>(args)...);
}

// The hook a future language plugs into: return the translated text for an ID, or an empty
// string to fall back to English.
void setLanguageLookup(std::function<QString(std::string_view)> lookup);

// Fills "{hk:<action id>}" in catalogue text with that hotkey's current keys, so a tooltip never
// names a key the user has changed. Installed once by the main window; without it a token is shown
// as written.
void setTokenResolver(std::function<QString(const QString& actionId)> resolver);

// Installs the ToolTip event filter on the application. Call once, after QApplication exists.
void install(QApplication& app);

// The text hovering `w` shows: its own or an ancestor's tooltip or ID, or for a form row label,
// its field's. Empty when there is none. What the event filter shows, exposed for tests.
QString hoverText(const QWidget* w);

// The nearest ID on `w` or its ancestors, or an empty view.
std::string_view resolveId(const QWidget* w);

// Every ID in the catalogue, for the coverage audit.
std::vector<std::string_view> catalogueIds();

} // namespace UiTooltips
