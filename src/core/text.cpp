// SPDX-License-Identifier: GPL-2.0-only
#include "core/text.hpp"

#include <charconv>
#include <cstdio>

namespace kaibo {

UiLocale localeFromTag(const std::string &tag)
{
	if (tag.rfind("zh", 0) != 0)
		return UiLocale::English;
	// zh-TW, zh-HK, zh-MO and script-tagged zh-Hant use Traditional characters.
	if (tag.find("TW") != std::string::npos || tag.find("HK") != std::string::npos ||
	    tag.find("MO") != std::string::npos || tag.find("Hant") != std::string::npos)
		return UiLocale::TraditionalChinese;
	return UiLocale::SimplifiedChinese;
}

std::string urlEncode(const std::string &value)
{
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(value.size() * 3);
	for (unsigned char c : value) {
		const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
					c == '-' || c == '_' || c == '.' || c == '~';
		if (unreserved) {
			out.push_back(static_cast<char>(c));
		} else {
			out.push_back('%');
			out.push_back(hex[c >> 4]);
			out.push_back(hex[c & 0x0F]);
		}
	}
	return out;
}

std::string formEncode(const std::vector<std::pair<std::string, std::string>> &fields)
{
	std::string out;
	for (const auto &[key, value] : fields) {
		if (!out.empty())
			out.push_back('&');
		out += urlEncode(key);
		out.push_back('=');
		out += urlEncode(value);
	}
	return out;
}

std::string queryValue(const std::string &url, const std::string &key)
{
	const size_t query = url.find('?');
	if (query == std::string::npos)
		return {};
	size_t pos = query + 1;
	while (pos < url.size()) {
		size_t end = url.find_first_of("&#", pos);
		if (end == std::string::npos)
			end = url.size();
		const std::string pair = url.substr(pos, end - pos);
		const size_t eq = pair.find('=');
		if (pair.substr(0, eq) == key)
			return eq == std::string::npos ? std::string() : pair.substr(eq + 1);
		if (end < url.size() && url[end] == '#')
			break;
		pos = end + 1;
	}
	return {};
}

size_t utf8Length(const std::string &text)
{
	size_t count = 0;
	for (size_t i = 0; i < text.size();) {
		const unsigned char c = static_cast<unsigned char>(text[i]);
		size_t width = 1;
		if (c >= 0xF0 && c <= 0xF4)
			width = 4;
		else if (c >= 0xE0)
			width = 3;
		else if (c >= 0xC2 && c <= 0xDF)
			width = 2;
		if (i + width > text.size())
			width = 1;
		for (size_t k = 1; k < width; ++k) {
			if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) {
				width = 1;
				break;
			}
		}
		i += width;
		++count;
	}
	return count;
}

static std::string oneDecimal(double value)
{
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%.1f", value);
	std::string text = buffer;
	if (text.size() > 2 && text.compare(text.size() - 2, 2, ".0") == 0)
		text.resize(text.size() - 2);
	return text;
}

static std::string grouped(int64_t value)
{
	std::string digits = std::to_string(value < 0 ? -value : value);
	std::string out;
	int n = 0;
	for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
		if (n && n % 3 == 0)
			out.insert(out.begin(), ',');
		out.insert(out.begin(), *it);
		++n;
	}
	return value < 0 ? "-" + out : out;
}

std::string formatCount(int64_t value, UiLocale locale)
{
	if (value < 0)
		value = 0;
	// The thresholds are "when rounded to one decimal this reads 1000 of the smaller
	// unit": 999999 is 1M, not 1000K; 99999999 is 1亿, not 10000万.
	constexpr int64_t kEnglishMillion = 999950;          // 999.95 K rounds to 1000 K
	constexpr int64_t kChineseHundredMillion = 99999500; // 9999.95 萬 rounds to 10000 萬
	if (locale == UiLocale::English) {
		if (value >= kEnglishMillion)
			return oneDecimal(static_cast<double>(value) / 1000000.0) + "M";
		if (value >= 10000)
			return oneDecimal(static_cast<double>(value) / 1000.0) + "K";
		return grouped(value);
	}
	const bool traditional = locale == UiLocale::TraditionalChinese;
	if (value >= kChineseHundredMillion)
		return oneDecimal(static_cast<double>(value) / 100000000.0) +
		       (traditional ? "\xE5\x84\x84" : "\xE4\xBA\xBF"); // 億 / 亿
	if (value >= 10000)
		return oneDecimal(static_cast<double>(value) / 10000.0) +
		       (traditional ? "\xE8\x90\xAC" : "\xE4\xB8\x87"); // 萬 / 万
	return grouped(value);
}

std::string formatClock(int64_t seconds)
{
	if (seconds < 0)
		seconds = 0;
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%02lld:%02lld:%02lld", static_cast<long long>(seconds / 3600),
		      static_cast<long long>((seconds / 60) % 60), static_cast<long long>(seconds % 60));
	return buffer;
}

bool parseInt(const std::string &text, int64_t &out)
{
	const std::string t = trim(text);
	if (t.empty())
		return false;
	int64_t value = 0;
	const auto result = std::from_chars(t.data(), t.data() + t.size(), value);
	if (result.ec != std::errc() || result.ptr != t.data() + t.size())
		return false;
	out = value;
	return true;
}

std::string trim(const std::string &text)
{
	const char *ws = " \t\r\n";
	const size_t begin = text.find_first_not_of(ws);
	if (begin == std::string::npos)
		return {};
	const size_t end = text.find_last_not_of(ws);
	return text.substr(begin, end - begin + 1);
}

} // namespace kaibo
