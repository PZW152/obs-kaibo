// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace kaibo {

// An ordered name → value cookie set for one site, as Bilibili's web API needs it.
class CookieJar {
public:
	CookieJar() = default;
	// Parses a "Cookie:" header value ("a=1; b=2").
	static CookieJar fromHeader(const std::string &header);

	std::string header() const;
	std::string get(const std::string &name) const;
	void set(const std::string &name, const std::string &value);
	// Applies Set-Cookie "name=value" pairs; an empty value or "deleted" removes the cookie.
	void apply(const std::vector<std::string> &setCookies);
	bool empty() const { return m_items.empty(); }
	void clear() { m_items.clear(); }

	// The cookies a signed-in web session needs (SESSDATA + bili_jct). DedeUserID is not
	// required: it can be filled in from the profile (BiliClient::fetchProfile) and the
	// session stays valid without it, so requiring it would sign out old saved jars.
	bool hasSession() const { return !get("SESSDATA").empty() && !get("bili_jct").empty(); }
	std::string csrf() const { return get("bili_jct"); }
	std::string uid() const { return get("DedeUserID"); }

private:
	std::vector<std::pair<std::string, std::string>> m_items;
};

} // namespace kaibo
