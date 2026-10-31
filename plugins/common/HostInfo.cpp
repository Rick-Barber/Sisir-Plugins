#include "HostInfo.h"

#include <map>
#include <string>

namespace SisirDcc {

namespace {
HostInfo& current() {
	static HostInfo info;
	return info;
}
} // namespace

const HostInfo& host() { return current(); }

void setHost(const HostInfo& info) { current() = info; }

const char* importTooltipId(const char* control) {
	// Kept for the process's life: UiTooltips takes ids as string views.
	static std::map<std::string, std::string> ids;
	const std::string key = current().tooltipPrefix.toStdString() + ".import." + control;
	return ids.emplace(key, key).first->second.c_str();
}

} // namespace SisirDcc
