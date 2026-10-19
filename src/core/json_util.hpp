// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "vendor/json11/json11.hpp"

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
		bool negative = false;
		size_t i = 0;
		if (s[0] == '-') {
			negative = true;
			i = 1;
		}
		if (i >= s.size())
			return false;
		for (; i < s.size(); ++i) {
			if (s[i] < '0' || s[i] > '9')
				return false;
			v = v * 10 + (s[i] - '0');
		}
		out = negative ? -v : v;
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
