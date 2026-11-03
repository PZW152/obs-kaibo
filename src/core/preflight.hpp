// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kaibo {

// Bilibili's limits for the two text fields Kaibo writes.
constexpr int kMaxTitleLength = 40;
constexpr int kMaxNewsLength = 60;

// A check Kaibo can run before a go-live request, using only its cached state (no
// network). The rules live in this dependency-free header so they are unit-tested;
// LiveController turns each code into a localized message and decides what blocks.
enum class PreflightCode {
	Connecting, // a sign-in check is in flight; wait for its result
	NotSignedIn,
	NoRoom, // the account has no live room yet
	PickArea,
	AreaLocked, // Bilibili requires an application for this area first
	TitleTooLong,
	NewsTooLong,
	NoTitle,          // advisory: the broadcast would open without a title
	Offline,          // advisory: the last network call failed
	AreasUnavailable, // advisory: the area list is missing, so the choice is unchecked
	RoomLive,         // advisory: the room already is live
	UnsyncedChanges,  // advisory: going live pushes the pending edits
};

enum class PreflightSeverity { Warning, Error };

struct PreflightIssue {
	PreflightCode code = PreflightCode::Connecting;
	PreflightSeverity severity = PreflightSeverity::Warning;
	int argument = 0; // %1 for the localized message; 0 when the message takes none
};

// Everything the checks look at. All fields are already-normalized (trimmed text,
// known area, ...) so the rules stay simple.
struct PreflightInput {
	bool signedIn = false;
	bool sessionChecking = false; // Auth::Checking
	bool offline = false;
	bool roomLoaded = false;
	int64_t roomId = 0;
	bool roomLive = false;
	int areaId = 0;
	bool areaLocked = false;
	bool areasLoaded = false;
	bool areasLoading = false;
	std::size_t titleLength = 0;
	std::size_t newsLength = 0;
	bool newsLoaded = false;
	bool unsyncedChanges = false;
};

// Errors first, then warnings, in a stable order. An empty result means "nothing to
// say": warnings never block, errors do.
inline std::vector<PreflightIssue> computePreflight(const PreflightInput &in)
{
	std::vector<PreflightIssue> errors;
	std::vector<PreflightIssue> warnings;

	if (!in.signedIn) {
		// Nothing else can be judged without a session; during the initial check,
		// "connecting" is not an error yet.
		if (in.sessionChecking)
			return {{PreflightCode::Connecting, PreflightSeverity::Warning, 0}};
		return {{PreflightCode::NotSignedIn, PreflightSeverity::Error, 0}};
	}

	if (in.roomId <= 0) {
		errors.push_back({PreflightCode::NoRoom, PreflightSeverity::Error, 0});
	} else {
		if (in.areaId <= 0)
			errors.push_back({PreflightCode::PickArea, PreflightSeverity::Error, 0});
		else if (in.areaLocked)
			errors.push_back({PreflightCode::AreaLocked, PreflightSeverity::Error, 0});
	}
	if (in.titleLength > static_cast<std::size_t>(kMaxTitleLength))
		errors.push_back({PreflightCode::TitleTooLong, PreflightSeverity::Error, kMaxTitleLength});
	if (in.newsLoaded && in.newsLength > static_cast<std::size_t>(kMaxNewsLength))
		errors.push_back({PreflightCode::NewsTooLong, PreflightSeverity::Error, kMaxNewsLength});

	if (in.offline)
		warnings.push_back({PreflightCode::Offline, PreflightSeverity::Warning, 0});
	if (!in.areasLoaded && !in.areasLoading)
		warnings.push_back({PreflightCode::AreasUnavailable, PreflightSeverity::Warning, 0});
	if (in.titleLength == 0)
		warnings.push_back({PreflightCode::NoTitle, PreflightSeverity::Warning, 0});
	if (in.roomLive)
		warnings.push_back({PreflightCode::RoomLive, PreflightSeverity::Warning, 0});
	if (in.unsyncedChanges)
		warnings.push_back({PreflightCode::UnsyncedChanges, PreflightSeverity::Warning, 0});

	errors.insert(errors.end(), warnings.begin(), warnings.end());
	return errors;
}

inline bool preflightBlocked(const std::vector<PreflightIssue> &issues)
{
	for (const PreflightIssue &issue : issues)
		if (issue.severity == PreflightSeverity::Error)
			return true;
	return false;
}

} // namespace kaibo
