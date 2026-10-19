// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>
#include <vector>

namespace kaibo {

struct HttpRequest {
	std::string method = "GET";
	std::string url;
	std::vector<std::string> headers;
	std::string body;
	long timeoutMs = 10000;
};

struct HttpResponse {
	long status = 0;
	std::string body;
	// "name=value" of every Set-Cookie header, attributes stripped, in order.
	std::vector<std::string> setCookies;
	// Transport failure (DNS, TLS, timeout...). Empty when a response arrived.
	std::string transportError;
	bool timedOut = false;

	bool transportOk() const { return transportError.empty(); }
};

// The only seam between the Bilibili client and the network. The production
// implementation is libcurl (app/curl_transport.cpp); tests use a fake.
class HttpTransport {
public:
	virtual ~HttpTransport() = default;
	virtual HttpResponse send(const HttpRequest &request) = 0;
};

} // namespace kaibo
