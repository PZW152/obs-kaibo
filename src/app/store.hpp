// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "core/cookies.hpp"
#include "core/settings_model.hpp"

#include <string>

namespace kaibo {

// Owns every file Kaibo writes. All of them live in one directory,
// <OBS config>/plugin_config/obs-kaibo/ (inside the OBS folder in portable mode):
//   settings.json      options, recents, stream-service backup   (+ .bak)
//   credentials.bin    the Bilibili session, sealed per OS user  (+ .bak)
// Nothing is written anywhere else; deleteAll() removes exactly these files.
class Store {
public:
	Store();

	const std::string &directory() const { return m_dir; }

	Settings loadSettings();
	bool saveSettings(const Settings &settings);

	CookieJar loadCredentials();
	bool saveCredentials(const CookieJar &jar);
	void clearCredentials();

	// Session cookies of the previous plugin ("BILIBILI Stream for OBS",
	// bilibili-stream-for-obs) if it left any. Read-only: never modified or deleted.
	CookieJar findLegacyCredentials() const;

	// Removes every file listed above, then the directory if it is empty.
	// Returns false (and leaves what it could not delete) on failure.
	bool deleteAll(std::string &error);

private:
	std::string path(const char *name) const;
	std::string m_dir;
};

} // namespace kaibo
