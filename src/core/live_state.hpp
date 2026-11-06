// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "core/areas.hpp"
#include "core/bili_client.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace kaibo {

// The decisions LiveController makes from its state, kept free of Qt and OBS so they are
// unit-tested. The controller owns the flows and side effects; these only answer questions.

// ---------------------------------------------------------------- phase

// Room state × OBS output state, as the person should understand it.
enum class LivePhase { Offline, Live, Reconnecting, RoomOnly, Dropped, StreamOnly };

struct PhaseInput {
	bool signedIn = false;
	bool roomLive = false;
	bool obsStreaming = false;
	bool streamTargetIsBilibili = false;
	bool reconnecting = false;
	bool dropped = false;
};

inline LivePhase derivePhase(const PhaseInput &in)
{
	// "Live" means the room is live AND OBS is actually pushing to Bilibili. OBS could
	// be streaming to another destination while the room is open; that is not a working
	// broadcast and must not look like one.
	if (in.roomLive && in.obsStreaming && in.streamTargetIsBilibili)
		return in.reconnecting ? LivePhase::Reconnecting : LivePhase::Live;
	if (in.roomLive)
		return in.dropped ? LivePhase::Dropped : LivePhase::RoomOnly;
	if (in.obsStreaming && in.streamTargetIsBilibili && in.signedIn)
		return LivePhase::StreamOnly;
	return LivePhase::Offline;
}

// ---------------------------------------------------------------- room edits

// What the person changed compared with what Bilibili has. Saving and going live send
// exactly this, and the dock's "unsaved" state is this being non-empty, so the three can
// never disagree.
struct RoomEdits {
	std::optional<std::string> title;
	std::optional<int> area;
	std::optional<std::string> news;

	bool any() const { return title || area || news; }
};

// All text already trimmed by the caller.
struct RoomDrafts {
	std::string title;
	int area = 0;
	bool newsLoaded = false; // without a loaded announcement there is nothing to compare
	std::string news;
	std::string savedNews;
};

inline RoomEdits diffRoom(const RoomInfo &room, const RoomDrafts &drafts)
{
	RoomEdits edits;
	if (!drafts.title.empty() && drafts.title != room.title)
		edits.title = drafts.title;
	if (drafts.area > 0 && drafts.area != room.areaId)
		edits.area = drafts.area;
	if (drafts.newsLoaded && drafts.news != drafts.savedNews)
		edits.news = drafts.news;
	return edits;
}

// Records edits Bilibili accepted, so the local copy matches without another request.
inline void applyRoomEdits(const RoomEdits &edits, const std::vector<AreaGroup> &areas, RoomInfo &room,
			   std::string &savedNews)
{
	if (edits.title)
		room.title = *edits.title;
	if (edits.area) {
		room.areaId = *edits.area;
		if (const Area *area = findArea(areas, *edits.area)) {
			room.areaName = area->name;
			room.parentAreaId = area->parentId;
			room.parentAreaName = area->parentName;
		}
	}
	if (edits.news)
		savedNews = *edits.news;
}

// ---------------------------------------------------------------- polling

constexpr int kPollBroadcastingMs = 30 * 1000;
constexpr int kPollIdleVisibleMs = 60 * 1000;
constexpr int kPollIdleHiddenMs = 5 * 60 * 1000;

// Room/get_info is public and answers normally for an expired session, so the room poll
// alone can never notice one: an authenticated call has to ask, this often.
constexpr int kSessionCheckMs = 30 * 60 * 1000;

struct PollInput {
	bool broadcasting = false; // the room is live, or OBS is streaming or connecting
	bool dockVisible = false;
	int offlineStreak = 0; // consecutive polls that could not reach Bilibili
};

inline int pollIntervalMs(const PollInput &in)
{
	// During a broadcast a stale dock is a real problem and the viewer count is on screen,
	// so the rate stays up even while offline: that is when reconnecting matters most.
	if (in.broadcasting)
		return kPollBroadcastingMs;
	const int base = in.dockVisible ? kPollIdleVisibleMs : kPollIdleHiddenMs;
	if (in.offlineStreak <= 0)
		return base;
	const int doublings = (std::min)(in.offlineStreak - 1, 4);
	return (std::max)(base, (std::min)(kPollBroadcastingMs << doublings, kPollIdleHiddenMs));
}

} // namespace kaibo
