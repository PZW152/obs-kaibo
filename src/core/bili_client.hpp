// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "core/areas.hpp"
#include "core/cookies.hpp"
#include "core/http.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kaibo {

enum class ErrorKind {
	None,
	Network,     // no response (offline, DNS, TLS, timeout)
	Http,        // non-2xx status; `code` holds it
	Parse,       // response was not the JSON we expected
	Api,         // Bilibili returned a non-zero code; `code`/`message` hold it
	NotLoggedIn, // session missing or expired (-101)
};

struct ApiError {
	ErrorKind kind = ErrorKind::None;
	int code = 0;
	std::string message; // Bilibili's own text when it gave one; otherwise technical detail

	explicit operator bool() const { return kind != ErrorKind::None; }
	static ApiError none() { return {}; }
};

struct QrTicket {
	std::string url;
	std::string key;
};

enum class QrState { Waiting, Scanned, Expired, Confirmed };

struct Profile {
	int64_t mid = 0;
	std::string name;
	std::string face;
};

struct RoomInfo {
	int64_t roomId = 0;
	std::string title;
	int areaId = 0;
	int parentAreaId = 0;
	std::string areaName;
	std::string parentAreaName;
	bool live = false;
	int64_t online = 0;
	int64_t followers = 0;
	int64_t liveStartedAt = 0; // Unix seconds, 0 when unknown or offline
};

struct LiveStart {
	std::string server;
	std::string key;
	std::string faceAuthUrl; // set when Bilibili requires face verification first
};

// Error codes the UI reacts to specifically.
namespace codes {
constexpr int NotLoggedIn = -101;
constexpr int FaceAuthRequired = 60024;
constexpr int FaceAuthRequiredAlt = 60043;
} // namespace codes

// Synchronous Bilibili web/live API client. Not thread-safe: run every call on the
// one worker thread. Holds the session cookies and updates them from Set-Cookie.
class BiliClient {
public:
	explicit BiliClient(HttpTransport &transport) : m_http(transport) {}

	void setCookies(CookieJar jar) { m_cookies = std::move(jar); }
	// Per-request timeout; shortened when OBS is exiting.
	void setTimeoutMs(long timeoutMs) { m_timeoutMs = timeoutMs; }
	const CookieJar &cookies() const { return m_cookies; }

	ApiError requestQr(QrTicket &ticket);
	// On Confirmed, the session cookies have been stored in this client.
	ApiError pollQr(const std::string &key, QrState &state);

	ApiError fetchProfile(Profile &profile);
	// roomId is 0 when the account has never opened a live room.
	ApiError fetchRoomId(int64_t mid, int64_t &roomId);
	ApiError fetchRoomInfo(int64_t roomId, RoomInfo &info);
	ApiError fetchRoomNews(int64_t roomId, std::string &content);
	ApiError fetchAreas(std::vector<AreaGroup> &groups);
	ApiError updateRoom(int64_t roomId, const std::optional<std::string> &title, std::optional<int> areaId);
	ApiError updateNews(int64_t roomId, int64_t mid, const std::string &content);
	ApiError startLive(int64_t roomId, int areaId, LiveStart &result);
	ApiError stopLive(int64_t roomId);
	ApiError logout();
	ApiError fetchBytes(const std::string &url, std::string &bytes);

	// Device id lookup used by ensureDeviceCookies(); exposed for tests.
	ApiError fetchDeviceFingerprint(std::string &buvid3, std::string &buvid4);
	// Bilibili's web client mints a stable device identity (buvid3/buvid4) once and
	// sends it with every request. Without one the live APIs see a new device on each
	// call, which is one of the things that makes Bilibili ask for face verification
	// again and again. An existing identity is never replaced. Best effort: on failure
	// the cookies stay as they were.
	ApiError ensureDeviceCookies(int64_t nowSeconds);

	// Exposed for tests: Bilibili's app signature over sorted, URL-encoded params.
	static std::string signParams(std::vector<std::pair<std::string, std::string>> params);
	// "2026-10-20 21:05:00" in China Standard Time → Unix seconds; 0 when invalid.
	static int64_t parseBeijingTime(const std::string &text);

private:
	// `appIdentity` marks endpoints called with the live-companion app key and
	// signature: those are client calls, so they must not also claim to come from the
	// web live centre (see the header note in the .cpp).
	HttpResponse get(const std::string &url, bool withCookies = true, bool appIdentity = false);
	HttpResponse post(const std::string &url, const std::string &form, bool appIdentity = false);
	std::vector<std::string> headers(bool withCookies, bool form, bool appIdentity) const;
	void absorbCookies(const HttpResponse &response);

	HttpTransport &m_http;
	CookieJar m_cookies;
	long m_timeoutMs = 10000;
};

} // namespace kaibo
