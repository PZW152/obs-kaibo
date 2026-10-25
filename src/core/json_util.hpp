// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "vendor/json11/json11.hpp"

#include <charconv>
#include <cstdint>
#include <string>

namespace kaibo {

// Bilibili returns ids as numbers on some endpoints and as strings on others.
// Accept both; never throw.
inline bool jsonInt(const json11::Json &value, int64_t &out)
{
	if (value.is_number()) {
		out = static_cast<int64_t>(value.number_value());
		return true;
	}
	if (value.is_string()) {
		const std::string &s = value.string_value();
		if (s.empty())
			return false;
		int64_t v = 0;
		// from_chars rejects out-of-range input instead of overflowing (UB).
		const auto result = std::from_chars(s.data(), s.data() + s.size(), v);
		if (result.ec != std::errc() || result.ptr != s.data() + s.size())
			return false;
		out = v;
		return true;
	}
	if (value.is_bool()) {
		out = value.bool_value() ? 1 : 0;
		return true;
	}
	return false;
}

inline int64_t jsonIntOr(const json11::Json &value, int64_t fallback = 0)
{
	int64_t out = fallback;
	return jsonInt(value, out) ? out : fallback;
}

} // namespace kaibo
