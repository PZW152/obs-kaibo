// Fake OBS and network layer for rendering the real dock offscreen.
#include "app/curl_transport.hpp"
#include "app/obs_bridge.hpp"
#include <obs-module.h>
#include <plugin-support.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>

static const std::string kData = KB_DATA_DIR;

static std::map<std::string, std::string> &texts()
{
	static std::map<std::string, std::string> map;
	if (map.empty()) {
		const char *lang = std::getenv("KB_LANG");
		std::ifstream f(kData + "locale/" + (lang ? lang : "zh-CN") + ".ini", std::ios::binary);
		std::string line;
		while (std::getline(f, line)) {
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			const size_t eq = line.find('=');
			if (eq == std::string::npos)
				continue;
			std::string v = line.substr(eq + 1);
			if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
				v = v.substr(1, v.size() - 2);
			size_t p;
			while ((p = v.find("\\n")) != std::string::npos)
				v.replace(p, 2, "\n");
			map[line.substr(0, eq)] = v;
		}
	}
	return map;
}

extern "C" const char *obs_module_text(const char *key)
{
	auto it = texts().find(key);
	return it == texts().end() ? key : it->second.c_str();
}
extern "C" const char *obs_get_locale(void)
{
	const char *lang = std::getenv("KB_LANG");
	return lang ? lang : "zh-CN";
}
extern "C" char *obs_module_file(const char *file)
{
	return strdup((kData + file).c_str());
}
extern "C" char *obs_module_config_path(const char *file)
{
	const char *tmp = std::getenv("KB_CFG");
	return strdup((std::string(tmp ? tmp : "/tmp/kaibo-preview/") + file).c_str());
}
extern "C" void obs_log(int, const char *, ...) {}

namespace kaibo {
CurlTransport::~CurlTransport() {}
void CurlTransport::dropHandle() {}
void CurlTransport::globalInit() {}
HttpResponse CurlTransport::send(const HttpRequest &)
{
	HttpResponse r;
	r.transportError = "preview";
	return r;
}
namespace obs {
bool captureStreamService(ServiceBackup &) { return false; }
bool applyStreamService(const std::string &, const std::string &, std::string &) { return true; }
bool restoreStreamService(const ServiceBackup &, std::string &) { return true; }
bool streamingActive() { return false; }
bool streamTargetIsBilibili() { return true; }
void startStreaming() {}
void stopStreaming() {}
void OutputWatcher::attach() {}
void OutputWatcher::detach() {}
void OutputWatcher::onStop(void *, ::calldata *) {}
void OutputWatcher::onReconnect(void *, ::calldata *) {}
void OutputWatcher::onReconnectSuccess(void *, ::calldata *) {}
} // namespace obs
} // namespace kaibo
