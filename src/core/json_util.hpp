// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "vendor/json11/json11.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>

namespace kaibo {

// Bilibili returns ids as numbers on some endpoints and as strings on others.
// Accept both; never throw.
inline bool jsonInt(const json11::Json &value, int64_t &out)
{
	if (value.is_number()) {
		// json11 stores every number as a double. Casting an out-of-range double to
		// int64_t is undefined behaviour, and a huge or non-finite number is server
		// data we cannot use anyway, so treat it as "not an int" instead.
		const double number = value.number_value();
		constexpr double kMin = -9223372036854775808.0; // -2^63, exactly representable
		constexpr double kMax = 9223372036854775808.0;  // +2^63, one past INT64_MAX
		if (!(number >= kMin && number < kMax))         // also false for NaN
			return false;
		// Ids, statuses and counts are whole numbers: a fractional value here is not
		// something any caller can use, and truncating it silently would be wrong.
		if (std::trunc(number) != number)
			return false;
		out = static_cast<int64_t>(number);
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
