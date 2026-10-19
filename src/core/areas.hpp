// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>
#include <vector>

namespace json11 {
class Json;
}

namespace kaibo {

struct Area {
	int id = 0;
	std::string name;
	int parentId = 0;
	std::string parentName;
	bool locked = false; // the streamer must apply to Bilibili before using it
};

struct AreaGroup {
	int id = 0;
	std::string name;
	std::vector<Area> areas;
};

// Parses the `data` array of /room/v1/Area/getList. Malformed entries are skipped, never thrown on.
std::vector<AreaGroup> parseAreaList(const json11::Json &data);

const Area *findArea(const std::vector<AreaGroup> &groups, int id);

// Groups and areas whose name or parent name contains `query` (ASCII case-insensitive).
// A group whose own name matches keeps all of its areas. Empty query returns everything.
std::vector<AreaGroup> searchAreas(const std::vector<AreaGroup> &groups, const std::string &query);

// Moves `id` to the front of a most-recently-used list, capped at `limit`.
void touchRecent(std::vector<int> &recent, int id, size_t limit = 6);

} // namespace kaibo
