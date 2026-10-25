// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>
#include <vector>

namespace kaibo {

struct Options {
	bool autoStartStream = true;       // start OBS streaming right after the room opens
	bool closeRoomOnStreamStop = true; // a deliberate stop in OBS also closes the room
	bool closeRoomOnExit = true;       // closing OBS closes the room
	bool restoreService = false;       // put the previous OBS stream settings back after ending
	bool confirmEnd = true;            // ask before ending
};

// The OBS stream service that was active before Kaibo replaced it. It is kept in a
// separate sealed file (service-backup.bin) because the JSON can contain another
// platform's stream key; settings.json never stores it.
struct ServiceBackup {
	std::string type;         // e.g. "rtmp_common"
	std::string settingsJson; // obs_data JSON of that service
	bool valid() const { return !type.empty(); }
};

enum class LegacyImport { Pending, Done, Dismissed };

// Everything in settings.json. Credentials live in a separate, protected file.
struct Settings {
	static constexpr int kSchema = 1;

	Options options;
	std::vector<int> recentAreas;
	std::vector<std::string> recentTitles;
	ServiceBackup serviceBackup;
	LegacyImport legacyImport = LegacyImport::Pending;
};

// Serializes to pretty JSON with a "schema" field.
std::string settingsToJson(const Settings &settings);

// Parses settings.json. Unknown keys are ignored, missing keys keep defaults, and a
// newer schema is read best-effort (never discarded). Returns false only when the
// text is not a JSON object at all; `out` then holds defaults.
bool settingsFromJson(const std::string &text, Settings &out);

void rememberTitle(std::vector<std::string> &titles, const std::string &title, size_t limit = 8);

// Writes `data` to `path` atomically: temp file in the same directory, flush, rename.
// The previous file is kept as `<path>.bak`. Paths are UTF-8.
bool writeFileAtomic(const std::string &path, const std::string &data, std::string &error);
bool readFile(const std::string &path, std::string &data);

} // namespace kaibo
