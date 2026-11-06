// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "core/http.hpp"

namespace kaibo {

// libcurl-backed transport. TLS verification stays on; redirects are not followed
// (the login flow needs the Set-Cookie headers of the redirecting response itself).
//
// One easy handle is kept between requests so its connection cache survives: a repeat
// request to the same host skips the TCP and TLS handshakes. That makes an instance
// single-threaded, which the worker already guarantees; anything else needs its own.
class CurlTransport : public HttpTransport {
public:
	CurlTransport() = default;
	~CurlTransport() override;
	CurlTransport(const CurlTransport &) = delete;
	CurlTransport &operator=(const CurlTransport &) = delete;

	static void globalInit();
	HttpResponse send(const HttpRequest &request) override;

private:
	void dropHandle();

	void *m_curl = nullptr; // CURL *; created on first use, after globalInit()
};

} // namespace kaibo
