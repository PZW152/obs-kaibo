// SPDX-License-Identifier: GPL-2.0-only
#include "app/curl_transport.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <mutex>

namespace kaibo {
namespace {

size_t onBody(char *data, size_t size, size_t count, void *user)
{
	auto *body = static_cast<std::string *>(user);
	const size_t bytes = size * count;
	// Hard cap: nothing Kaibo fetches is anywhere near 8 MiB.
	if (body->size() + bytes > 8 * 1024 * 1024)
		return 0;
	body->append(data, bytes);
	return bytes;
}

size_t onHeader(char *data, size_t size, size_t count, void *user)
{
	auto *cookies = static_cast<std::vector<std::string> *>(user);
	const size_t bytes = size * count;
	static const char prefix[] = "set-cookie:";
	const size_t prefixLength = sizeof(prefix) - 1;
	if (bytes <= prefixLength)
		return bytes;
	for (size_t i = 0; i < prefixLength; ++i)
		if (std::tolower(static_cast<unsigned char>(data[i])) != prefix[i])
			return bytes;

	std::string line(data + prefixLength, bytes - prefixLength);
	const size_t semicolon = line.find(';');
	if (semicolon != std::string::npos)
		line.resize(semicolon);
	const size_t begin = line.find_first_not_of(" \t");
	const size_t end = line.find_last_not_of(" \t\r\n");
	if (begin == std::string::npos || end < begin)
		return bytes;
	line = line.substr(begin, end - begin + 1);
	if (line.find('=') != std::string::npos)
		cookies->push_back(line);
	return bytes;
}

struct CurlDeleter {
	void operator()(CURL *curl) const { curl_easy_cleanup(curl); }
};
struct SlistDeleter {
	void operator()(curl_slist *list) const { curl_slist_free_all(list); }
};

} // namespace

void CurlTransport::globalInit()
{
	static std::once_flag once;
	std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

HttpResponse CurlTransport::send(const HttpRequest &request)
{
	HttpResponse response;
	std::unique_ptr<CURL, CurlDeleter> curl(curl_easy_init());
	if (!curl) {
		response.transportError = "curl_easy_init failed";
		return response;
	}

	curl_slist *raw = nullptr;
	for (const auto &header : request.headers)
		raw = curl_slist_append(raw, header.c_str());
	std::unique_ptr<curl_slist, SlistDeleter> headers(raw);

	CURL *c = curl.get();
	curl_easy_setopt(c, CURLOPT_URL, request.url.c_str());
	curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers.get());
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onBody);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &response.body);
	curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, onHeader);
	curl_easy_setopt(c, CURLOPT_HEADERDATA, &response.setCookies);
	curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, request.timeoutMs);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, (std::min)(6000L, request.timeoutMs));
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
#else
	curl_easy_setopt(c, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
	if (request.method == "POST") {
		curl_easy_setopt(c, CURLOPT_POST, 1L);
		curl_easy_setopt(c, CURLOPT_POSTFIELDS, request.body.c_str());
		curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
	}

	const CURLcode result = curl_easy_perform(c);
	if (result != CURLE_OK) {
		response.timedOut = result == CURLE_OPERATION_TIMEDOUT;
		response.transportError = curl_easy_strerror(result);
		response.body.clear();
		return response;
	}
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &response.status);
	return response;
}

} // namespace kaibo
