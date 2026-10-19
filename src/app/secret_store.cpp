// SPDX-License-Identifier: GPL-2.0-only
#include "app/secret_store.hpp"

#include <filesystem>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dpapi.h>
#endif

namespace kaibo::secret {
namespace {

// File layout: "KBS1" + scheme byte + payload. Scheme 1 = DPAPI, 0 = plain.
constexpr char kMagic[] = "KBS1";
constexpr size_t kMagicLength = 4;

#ifdef _WIN32
const char kEntropy[] = "Kaibo credentials v1";

DATA_BLOB blobOf(const std::string &s)
{
	DATA_BLOB b;
	b.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(s.data()));
	b.cbData = static_cast<DWORD>(s.size());
	return b;
}
#endif

} // namespace

std::string seal(const std::string &plain)
{
	std::string out(kMagic, kMagicLength);
#ifdef _WIN32
	DATA_BLOB in = blobOf(plain);
	DATA_BLOB entropy = blobOf(std::string(kEntropy));
	DATA_BLOB sealed{};
	if (CryptProtectData(&in, L"Kaibo", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &sealed)) {
		out.push_back('\x01');
		out.append(reinterpret_cast<const char *>(sealed.pbData), sealed.cbData);
		LocalFree(sealed.pbData);
		return out;
	}
#endif
	out.push_back('\x00');
	out += plain;
	return out;
}

bool open(const std::string &sealed, std::string &plain)
{
	if (sealed.size() < kMagicLength + 1 || sealed.compare(0, kMagicLength, kMagic) != 0)
		return false;
	const char scheme = sealed[kMagicLength];
	const std::string payload = sealed.substr(kMagicLength + 1);
	if (scheme == '\x00') {
		plain = payload;
		return true;
	}
#ifdef _WIN32
	if (scheme == '\x01') {
		DATA_BLOB in = blobOf(payload);
		DATA_BLOB entropy = blobOf(std::string(kEntropy));
		DATA_BLOB out{};
		if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
			return false;
		plain.assign(reinterpret_cast<const char *>(out.pbData), out.cbData);
		SecureZeroMemory(out.pbData, out.cbData);
		LocalFree(out.pbData);
		return true;
	}
#endif
	return false;
}

void restrictPermissions(const std::string &path)
{
#ifndef _WIN32
	std::error_code ec;
	std::filesystem::permissions(std::filesystem::u8path(path),
				     std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
				     std::filesystem::perm_options::replace, ec);
#else
	(void)path; // the per-user profile directory already restricts access; DPAPI binds to the user
#endif
}

const char *protectionName()
{
#ifdef _WIN32
	return "Windows DPAPI";
#else
	return "file permissions (0600)";
#endif
}

} // namespace kaibo::secret
