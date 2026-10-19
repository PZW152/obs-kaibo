// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>

namespace kaibo::secret {

// Encrypts `plain` for the current OS user. Windows: DPAPI (only this Windows
// account can decrypt). Other systems: stored as-is, protected by 0600 file
// permissions — see docs/privacy.md.
std::string seal(const std::string &plain);
// Returns false when the blob is corrupt or belongs to another user/machine.
bool open(const std::string &sealed, std::string &plain);
// Restricts a file to the current user where the OS supports it.
void restrictPermissions(const std::string &path);
// Human-readable protection in use, for the About section and logs.
const char *protectionName();

} // namespace kaibo::secret
