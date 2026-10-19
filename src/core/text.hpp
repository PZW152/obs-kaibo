// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace kaibo {

enum class UiLocale { SimplifiedChinese, TraditionalChinese, English };

// Maps an OBS locale ("zh-CN", "zh-TW", "en-US", ...) to the three locales Kaibo ships.
UiLocale localeFromTag(const std::string &tag);

// Percent-encodes everything except RFC 3986 unreserved characters.
std::string urlEncode(const std::string &value);

// "a=1&b=2" with each value URL-encoded, in the given order.
std::string formEncode(const std::vector<std::pair<std::string, std::string>> &fields);

// Value of `key` in a URL's query string (not decoded), or "" when absent.
std::string queryValue(const std::string &url, const std::string &key);

// Number of Unicode code points in a UTF-8 string (invalid bytes count as one each).
size_t utf8Length(const std::string &text);

// Compact counts: 1,284 / 1.2万 / 1.2萬 / 12.3K.
std::string formatCount(int64_t value, UiLocale locale);

// "01:24:36" (hours always shown, can exceed 24).
std::string formatClock(int64_t seconds);

// Parses a base-10 integer that may arrive as a JSON number or string. No exceptions.
bool parseInt(const std::string &text, int64_t &out);

std::string trim(const std::string &text);

} // namespace kaibo
