// SPDX-License-Identifier: GPL-2.0-only
#include "core/areas.hpp"
#include "core/json_util.hpp"
#include "core/text.hpp"

#include <algorithm>

namespace kaibo {

std::vector<AreaGroup> parseAreaList(const json11::Json &data)
{
	std::vector<AreaGroup> groups;
	if (!data.is_array())
		return groups;
	for (const auto &item : data.array_items()) {
		AreaGroup group;
		int64_t id = 0;
		if (!jsonInt(item["id"], id) || id <= 0)
			continue;
		group.id = static_cast<int>(id);
		group.name = item["name"].string_value();
		for (const auto &child : item["list"].array_items()) {
			Area area;
			int64_t childId = 0;
			if (!jsonInt(child["id"], childId) || childId <= 0)
				continue;
			area.id = static_cast<int>(childId);
			area.name = child["name"].string_value();
			area.parentId = group.id;
			area.parentName = group.name;
			int64_t lock = 0;
			area.locked = jsonInt(child["lock_status"], lock) && lock != 0;
			if (!area.name.empty())
				group.areas.push_back(std::move(area));
		}
		if (!group.areas.empty())
			groups.push_back(std::move(group));
	}
	return groups;
}

const Area *findArea(const std::vector<AreaGroup> &groups, int id)
{
	for (const auto &group : groups)
		for (const auto &area : group.areas)
			if (area.id == id)
				return &area;
	return nullptr;
}

static std::string asciiLower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		       [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
	return text;
}

std::vector<AreaGroup> searchAreas(const std::vector<AreaGroup> &groups, const std::string &query)
{
	const std::string needle = asciiLower(trim(query));
	if (needle.empty())
		return groups;

	std::vector<AreaGroup> out;
	for (const auto &group : groups) {
		if (asciiLower(group.name).find(needle) != std::string::npos) {
			out.push_back(group);
			continue;
		}
		AreaGroup hit{group.id, group.name, {}};
		for (const auto &area : group.areas)
			if (asciiLower(area.name).find(needle) != std::string::npos)
				hit.areas.push_back(area);
		if (!hit.areas.empty())
			out.push_back(std::move(hit));
	}
	return out;
}

void touchRecent(std::vector<int> &recent, int id, size_t limit)
{
	if (id <= 0)
		return;
	recent.erase(std::remove(recent.begin(), recent.end(), id), recent.end());
	recent.insert(recent.begin(), id);
	if (recent.size() > limit)
		recent.resize(limit);
}

} // namespace kaibo
