// SPDX-License-Identifier: GPL-2.0-only
#include "core/settings_model.hpp"
#include "core/json_util.hpp"
#include "core/text.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace kaibo {

std::string settingsToJson(const Settings &s)
{
	json11::Json::array areas(s.recentAreas.begin(), s.recentAreas.end());
	json11::Json::array titles(s.recentTitles.begin(), s.recentTitles.end());
	const char *legacy = s.legacyImport == LegacyImport::Done        ? "done"
			     : s.legacyImport == LegacyImport::Dismissed ? "dismissed"
									 : "pending";
	json11::Json json = json11::Json::object{
		{"schema", Settings::kSchema},
		{"options",
		 json11::Json::object{
			 {"autoStartStream", s.options.autoStartStream},
			 {"closeRoomOnStreamStop", s.options.closeRoomOnStreamStop},
			 {"closeRoomOnExit", s.options.closeRoomOnExit},
			 {"restoreService", s.options.restoreService},
			 {"confirmEnd", s.options.confirmEnd},
		 }},
		{"recentAreas", areas},
		{"recentTitles", titles},
		// serviceBackup is deliberately absent: its JSON can contain another platform's
		// stream key, so it lives in the sealed service-backup.bin instead.
		{"legacyImport", legacy},
	};
	return json.dump() + "\n";
}

static void readBool(const json11::Json &obj, const char *key, bool &out)
{
	if (obj[key].is_bool())
		out = obj[key].bool_value();
}

bool settingsFromJson(const std::string &text, Settings &out)
{
	out = Settings();
	std::string err;
	const auto json = json11::Json::parse(text, err);
	if (!err.empty() || !json.is_object())
		return false;

	const auto &o = json["options"];
	readBool(o, "autoStartStream", out.options.autoStartStream);
	readBool(o, "closeRoomOnStreamStop", out.options.closeRoomOnStreamStop);
	readBool(o, "closeRoomOnExit", out.options.closeRoomOnExit);
	readBool(o, "restoreService", out.options.restoreService);
	readBool(o, "confirmEnd", out.options.confirmEnd);

	for (const auto &item : json["recentAreas"].array_items()) {
		int64_t id = 0;
		if (jsonInt(item, id) && id > 0 && out.recentAreas.size() < 6)
			out.recentAreas.push_back(static_cast<int>(id));
	}
	for (const auto &item : json["recentTitles"].array_items())
		if (item.is_string() && !item.string_value().empty() && out.recentTitles.size() < 8)
			out.recentTitles.push_back(item.string_value());

	// Older versions stored the stream-service backup here in plaintext. Keep reading it
	// so an upgrade can move it into the sealed file, but never write it again.
	const auto &backup = json["serviceBackup"];
	if (backup.is_object() && backup["type"].is_string()) {
		out.serviceBackup.type = backup["type"].string_value();
		out.serviceBackup.settingsJson = backup["settings"].string_value();
	}

	const std::string legacy = json["legacyImport"].string_value();
	out.legacyImport = legacy == "done"        ? LegacyImport::Done
			   : legacy == "dismissed" ? LegacyImport::Dismissed
						   : LegacyImport::Pending;
	return true;
}

void rememberTitle(std::vector<std::string> &titles, const std::string &title, size_t limit)
{
	const std::string t = trim(title);
	if (t.empty())
		return;
	titles.erase(std::remove(titles.begin(), titles.end(), t), titles.end());
	titles.insert(titles.begin(), t);
	if (titles.size() > limit)
		titles.resize(limit);
}

namespace {

// writeFileAtomic promises the rename never publishes a half-written file; that also
// needs the bytes on disk, not just in the OS write cache.
bool flushToDisk(const fs::path &path, std::string &error)
{
#ifdef _WIN32
	HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
				    FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		error = "reopen temp file failed";
		return false;
	}
	const BOOL flushed = FlushFileBuffers(handle);
	CloseHandle(handle);
	if (!flushed) {
		error = "flush temp file failed";
		return false;
	}
	return true;
#else
	const int descriptor = ::open(path.c_str(), O_RDONLY);
	if (descriptor < 0) {
		error = "reopen temp file failed";
		return false;
	}
	const int flushed = ::fsync(descriptor);
	::close(descriptor);
	if (flushed != 0) {
		error = "flush temp file failed";
		return false;
	}
	return true;
#endif
}

} // namespace

bool writeFileAtomic(const std::string &path, const std::string &data, std::string &error)
{
	std::error_code ec;
	const fs::path target = fs::u8path(path);
	fs::create_directories(target.parent_path(), ec);
	if (ec) {
		error = "create directory: " + ec.message();
		return false;
	}

	fs::path temp = target;
	temp += ".tmp";
	{
		std::ofstream file(temp, std::ios::binary | std::ios::trunc);
		if (!file) {
			error = "open temp file failed";
			return false;
		}
#ifndef _WIN32
		// The credentials file travels through this path on macOS/Linux, where it is
		// plaintext protected only by its mode: never let another local account read the
		// temp file while it exists, or after a crash that leaves it behind.
		std::error_code permissionError;
		fs::permissions(temp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace,
				permissionError);
#endif
		file.write(data.data(), static_cast<std::streamsize>(data.size()));
		file.flush();
		if (!file) {
			error = "write temp file failed";
			file.close();
			fs::remove(temp, ec);
			return false;
		}
	}
	if (!flushToDisk(temp, error)) {
		fs::remove(temp, ec);
		return false;
	}

	if (fs::exists(target, ec)) {
		fs::path backup = target;
		backup += ".bak";
		fs::copy_file(target, backup, fs::copy_options::overwrite_existing, ec);
		ec.clear(); // a missing backup must not block saving
	}
	fs::rename(temp, target, ec);
	if (ec) {
		error = "replace file: " + ec.message();
		fs::remove(temp, ec);
		return false;
	}
	return true;
}

bool readFile(const std::string &path, std::string &data)
{
	std::ifstream file(fs::u8path(path), std::ios::binary);
	if (!file)
		return false;
	data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	return !file.bad();
}

} // namespace kaibo
