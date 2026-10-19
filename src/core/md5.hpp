// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>

namespace kaibo {

// Lower-case hex MD5 (RFC 1321). Used only for Bilibili's request signature, never for security.
std::string md5Hex(const std::string &data);

} // namespace kaibo
