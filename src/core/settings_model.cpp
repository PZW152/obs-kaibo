// SPDX-License-Identifier: GPL-2.0-only
#include "core/settings_model.hpp"
#include "core/json_util.hpp"
#include "core/text.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

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
		{"serviceBackup", s.serviceBackup.valid() ? json11::Json(json11::Json::object{
								    {"type", s.serviceBackup.type},
								    {"settings", s.serviceBackup.settingsJson},
							    })
							  : json11::Json(nullptr)},
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
		file.write(data.data(), static_cast<std::streamsize>(data.size()));
		file.flush();
		if (!file) {
			error = "write temp file failed";
			file.close();
			fs::remove(temp, ec);
			return false;
		}
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
