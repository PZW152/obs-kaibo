// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "core/http.hpp"

namespace kaibo {

// libcurl-backed transport. TLS verification stays on; redirects are not followed
// (the login flow needs the Set-Cookie headers of the redirecting response itself).
class CurlTransport : public HttpTransport {
public:
	static void globalInit();
	HttpResponse send(const HttpRequest &request) override;
};

} // namespace kaibo
