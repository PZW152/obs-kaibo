// SPDX-License-Identifier: GPL-2.0-only
#include "app/store.hpp"
#include "app/secret_store.hpp"
#include "vendor/json11/json11.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/bmem.h>

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace kaibo {
namespace {

const char *const kOwnedFiles[] = {"settings.json",      "settings.json.bak",      "settings.json.tmp",
				   "credentials.bin",    "credentials.bin.bak",    "credentials.bin.tmp",
				   "service-backup.bin", "service-backup.bin.bak", "service-backup.bin.tmp"};

std::string moduleConfigDir()
{
	char *raw = obs_module_config_path("");
	std::string dir = raw ? raw : "";
	bfree(raw);
	while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\'))
		dir.pop_back();
	return dir;
}

} // namespace

Store::Store() : m_dir(moduleConfigDir()) {}

std::string Store::path(const char *name) const
{
	return m_dir + "/" + name;
}

Settings Store::loadSettings()
{
	Settings settings;
	std::string text;
	if (!readFile(path("settings.json"), text)) {
		settings.serviceBackup = loadServiceBackup();
		return settings;
	}
	if (!settingsFromJson(text, settings)) {
		obs_log(LOG_WARNING, "settings.json is unreadable; trying the backup");
		if (!readFile(path("settings.json.bak"), text) || !settingsFromJson(text, settings))
			obs_log(LOG_WARNING, "no usable settings backup; using defaults");
	}
	const ServiceBackup sealed = loadServiceBackup();
	if (sealed.valid()) {
		settings.serviceBackup = sealed;
	} else if (settings.serviceBackup.valid()) {
		// 1.0.2 kept this in plaintext in settings.json: move it into the sealed file and
		// rewrite settings.json without it.
		saveServiceBackup(settings.serviceBackup);
		if (!saveSettings(settings))
			obs_log(LOG_WARNING, "could not remove the plaintext stream-service backup from settings.json");
	}
	return settings;
}

bool Store::saveSettings(const Settings &settings)
{
	std::string error;
	if (!writeFileAtomic(path("settings.json"), settingsToJson(settings), error)) {
		obs_log(LOG_WARNING, "could not save settings: %s", error.c_str());
		return false;
	}
	return true;
}

CookieJar Store::loadCredentials()
{
	std::string sealed;
	if (!readFile(path("credentials.bin"), sealed))
		return {};
	std::string plain;
	if (!secret::open(sealed, plain)) {
		obs_log(LOG_WARNING, "stored sign-in could not be decrypted (another user or machine); signing out");
		return {};
	}
	std::string err;
	const auto json = json11::Json::parse(plain, err);
	if (!err.empty())
		return {};
	return CookieJar::fromHeader(json["cookies"].string_value());
}

bool Store::saveCredentials(const CookieJar &jar)
{
	const std::string plain = json11::Json(json11::Json::object{{"v", 1}, {"cookies", jar.header()}}).dump();
	const std::string sealed = secret::seal(plain);
	if (sealed.empty()) {
		obs_log(LOG_WARNING, "could not encrypt the sign-in; nothing was written");
		return false;
	}
	std::string error;
	const std::string file = path("credentials.bin");
	if (!writeFileAtomic(file, sealed, error)) {
		obs_log(LOG_WARNING, "could not save sign-in: %s", error.c_str());
		return false;
	}
	secret::restrictPermissions(file);
	secret::restrictPermissions(file + ".bak");
	return true;
}

void Store::clearCredentials()
{
	std::error_code ec;
	for (const char *name : {"credentials.bin", "credentials.bin.bak", "credentials.bin.tmp"})
		fs::remove(fs::u8path(path(name)), ec);
}

ServiceBackup Store::loadServiceBackup()
{
	std::string sealed;
	if (!readFile(path("service-backup.bin"), sealed))
		return {};
	std::string plain;
	if (!secret::open(sealed, plain)) {
		obs_log(LOG_WARNING, "stored stream-service backup could not be decrypted; ignoring it");
		return {};
	}
	std::string err;
	const auto json = json11::Json::parse(plain, err);
	if (!err.empty() || !json.is_object())
		return {};
	ServiceBackup backup;
	backup.type = json["type"].string_value();
	backup.settingsJson = json["settings"].string_value();
	return backup;
}

bool Store::saveServiceBackup(const ServiceBackup &backup)
{
	if (!backup.valid()) {
		clearServiceBackup();
		return true;
	}
	const std::string plain =
		json11::Json(json11::Json::object{{"v", 1}, {"type", backup.type}, {"settings", backup.settingsJson}})
			.dump();
	const std::string sealed = secret::seal(plain);
	if (sealed.empty()) {
		obs_log(LOG_WARNING, "could not encrypt the stream-service backup; nothing was written");
		return false;
	}
	std::string error;
	const std::string file = path("service-backup.bin");
	if (!writeFileAtomic(file, sealed, error)) {
		obs_log(LOG_WARNING, "could not save the stream-service backup: %s", error.c_str());
		return false;
	}
	secret::restrictPermissions(file);
	secret::restrictPermissions(file + ".bak");
	return true;
}

void Store::clearServiceBackup()
{
	std::error_code ec;
	for (const char *name : {"service-backup.bin", "service-backup.bin.bak", "service-backup.bin.tmp"})
		fs::remove(fs::u8path(path(name)), ec);
}

CookieJar Store::findLegacyCredentials() const
{
	// <plugin_config>/obs-kaibo → <plugin_config>/bilibili-stream-for-obs/config.json
	const fs::path legacy = fs::u8path(m_dir).parent_path() / "bilibili-stream-for-obs" / "config.json";
	std::string text;
	std::error_code ec;
	if (!fs::exists(legacy, ec) || !readFile(legacy.u8string(), text))
		return {};
	std::string err;
	const auto json = json11::Json::parse(text, err);
	if (!err.empty())
		return {};
	auto jar = CookieJar::fromHeader(json["cookies"].string_value());
	return jar.hasSession() ? jar : CookieJar();
}

bool Store::deleteAll(std::string &error)
{
	bool ok = true;
	for (const char *name : kOwnedFiles) {
		std::error_code ec;
		const fs::path file = fs::u8path(path(name));
		if (fs::exists(file, ec) && !fs::remove(file, ec)) {
			ok = false;
			error += std::string(name) + ": " + ec.message() + "\n";
		}
	}
	std::error_code ec;
	const fs::path dir = fs::u8path(m_dir);
	if (fs::exists(dir, ec) && fs::is_empty(dir, ec))
		fs::remove(dir, ec);
	return ok;
}

} // namespace kaibo
