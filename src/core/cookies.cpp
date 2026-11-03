// SPDX-License-Identifier: GPL-2.0-only
#include "core/cookies.hpp"
#include "core/text.hpp"

#include <algorithm>

namespace kaibo {

CookieJar CookieJar::fromHeader(const std::string &header)
{
	CookieJar jar;
	size_t pos = 0;
	while (pos < header.size()) {
		size_t end = header.find(';', pos);
		if (end == std::string::npos)
			end = header.size();
		const std::string part = trim(header.substr(pos, end - pos));
		const size_t eq = part.find('=');
		if (eq != std::string::npos && eq > 0)
			jar.set(trim(part.substr(0, eq)), trim(part.substr(eq + 1)));
		pos = end + 1;
	}
	return jar;
}

std::string CookieJar::header() const
{
	std::string out;
	for (const auto &[name, value] : m_items) {
		if (!out.empty())
			out += "; ";
		out += name + "=" + value;
	}
	return out;
}

std::string CookieJar::get(const std::string &name) const
{
	for (const auto &[key, value] : m_items)
		if (key == name)
			return value;
	return {};
}

void CookieJar::set(const std::string &name, const std::string &value)
{
	// A cookie name is unique: a jar built from a header that listed the same name
	// twice must not keep sending both values (last value wins, like a browser). The
	// position of the first occurrence is kept so the header order stays stable.
	size_t first = m_items.size();
	for (size_t i = 0; i < m_items.size();) {
		if (m_items[i].first != name) {
			++i;
		} else if (first == m_items.size()) {
			first = i;
			++i;
		} else {
			m_items.erase(m_items.begin() + static_cast<std::ptrdiff_t>(i));
		}
	}
	if (value.empty() || value == "deleted") {
		if (first != m_items.size())
			m_items.erase(m_items.begin() + static_cast<std::ptrdiff_t>(first));
		return;
	}
	if (first != m_items.size())
		m_items[first].second = value;
	else
		m_items.emplace_back(name, value);
}

void CookieJar::apply(const std::vector<std::string> &setCookies)
{
	for (const auto &pair : setCookies) {
		const size_t eq = pair.find('=');
		if (eq == std::string::npos || eq == 0)
			continue;
		set(trim(pair.substr(0, eq)), trim(pair.substr(eq + 1)));
	}
}

} // namespace kaibo
