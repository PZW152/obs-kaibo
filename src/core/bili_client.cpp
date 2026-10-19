// SPDX-License-Identifier: GPL-2.0-only
#include "core/bili_client.hpp"
#include "core/json_util.hpp"
#include "core/md5.hpp"
#include "core/text.hpp"

#include <algorithm>
#include <ctime>

namespace kaibo {
namespace {

// Bilibili live companion ("直播姬") app credentials used by the start-live endpoint.
// They are public and identical in every open-source Bilibili live tool.
const char *const kAppKey = "aae92bc66f3edfab";
const char *const kAppSecret = "af125a0d5279fd576c1b4418a3e8276d";

const char *const kUserAgent =
	"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
	"Chrome/129.0.0.0 Safari/537.36";

ApiError networkError(const HttpResponse &response)
{
	return {ErrorKind::Network, 0, response.transportError};
}

// Turns a response into JSON plus the standard Bilibili envelope check.
ApiError parseEnvelope(const HttpResponse &response, json11::Json &json)
{
	if (!response.transportOk())
		return networkError(response);
	if (response.status < 200 || response.status >= 300)
		return {ErrorKind::Http, static_cast<int>(response.status), "HTTP " + std::to_string(response.status)};

	std::string err;
	json = json11::Json::parse(response.body, err);
	if (!err.empty() || !json.is_object())
		return {ErrorKind::Parse, 0, err.empty() ? "unexpected response" : err};

	const int code = static_cast<int>(jsonIntOr(json["code"]));
	if (code == 0)
		return ApiError::none();
	std::string message = json["message"].string_value();
	if (message.empty())
		message = json["msg"].string_value();
	if (code == codes::NotLoggedIn)
		return {ErrorKind::NotLoggedIn, code, message};
	return {ErrorKind::Api, code, message};
}

int64_t daysFromCivil(int64_t y, unsigned m, unsigned d)
{
	y -= m <= 2;
	const int64_t era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = static_cast<unsigned>(y - era * 400);
	const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

} // namespace

std::vector<std::string> BiliClient::headers(bool withCookies, bool form) const
{
	std::vector<std::string> list = {
		"Accept: application/json, text/plain, */*",
		"Accept-Language: zh-CN,zh;q=0.9,en;q=0.8",
		"Origin: https://link.bilibili.com",
		"Referer: https://link.bilibili.com/p/center/index",
		kUserAgent,
	};
	if (form)
		list.emplace_back("Content-Type: application/x-www-form-urlencoded; charset=UTF-8");
	if (withCookies && !m_cookies.empty())
		list.push_back("Cookie: " + m_cookies.header());
	return list;
}

HttpResponse BiliClient::get(const std::string &url, bool withCookies)
{
	HttpRequest request;
	request.url = url;
	request.headers = headers(withCookies, false);
	request.timeoutMs = m_timeoutMs;
	return m_http.send(request);
}

HttpResponse BiliClient::post(const std::string &url, const std::string &form)
{
	HttpRequest request;
	request.method = "POST";
	request.url = url;
	request.body = form;
	request.headers = headers(true, true);
	request.timeoutMs = m_timeoutMs;
	return m_http.send(request);
}

std::string BiliClient::signParams(std::vector<std::pair<std::string, std::string>> params)
{
	params.emplace_back("appkey", kAppKey);
	std::sort(params.begin(), params.end());
	const std::string query = formEncode(params);
	return query + "&sign=" + md5Hex(query + kAppSecret);
}

int64_t BiliClient::parseBeijingTime(const std::string &text)
{
	// Exactly "YYYY-MM-DD HH:MM:SS".
	static const char pattern[] = "dddd-dd-dd dd:dd:dd";
	if (text.size() != sizeof(pattern) - 1)
		return 0;
	for (size_t i = 0; i < text.size(); ++i) {
		const bool digit = text[i] >= '0' && text[i] <= '9';
		if (pattern[i] == 'd' ? !digit : text[i] != pattern[i])
			return 0;
	}
	auto field = [&text](size_t pos, size_t len) {
		int value = 0;
		for (size_t i = pos; i < pos + len; ++i)
			value = value * 10 + (text[i] - '0');
		return value;
	};
	const int y = field(0, 4), mo = field(5, 2), d = field(8, 2), h = field(11, 2), mi = field(14, 2),
		  s = field(17, 2);
	if (y < 2000 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60)
		return 0;
	const int64_t days = daysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d));
	return days * 86400 + h * 3600 + mi * 60 + s - 8 * 3600;
}

ApiError BiliClient::requestQr(QrTicket &ticket)
{
	json11::Json json;
	auto error =
		parseEnvelope(get("https://passport.bilibili.com/x/passport-login/web/qrcode/generate", false), json);
	if (error)
		return error;
	ticket.url = json["data"]["url"].string_value();
	ticket.key = json["data"]["qrcode_key"].string_value();
	if (ticket.url.empty() || ticket.key.empty())
		return {ErrorKind::Parse, 0, "missing qrcode url or key"};
	return ApiError::none();
}

ApiError BiliClient::pollQr(const std::string &key, QrState &state)
{
	const auto response = get(
		"https://passport.bilibili.com/x/passport-login/web/qrcode/poll?qrcode_key=" + urlEncode(key), false);
	json11::Json json;
	auto error = parseEnvelope(response, json);
	if (error)
		return error;

	const int64_t code = jsonIntOr(json["data"]["code"], -1);
	switch (code) {
	case 86101:
		state = QrState::Waiting;
		return ApiError::none();
	case 86090:
		state = QrState::Scanned;
		return ApiError::none();
	case 86038:
		state = QrState::Expired;
		return ApiError::none();
	case 0:
		break;
	default:
		return {ErrorKind::Api, static_cast<int>(code), json["data"]["message"].string_value()};
	}

	CookieJar jar;
	jar.apply(response.setCookies);
	const std::string successUrl = json["data"]["url"].string_value();
	if (!jar.hasSession() && !successUrl.empty()) {
		// Since 2026-08 the success URL is a crossDomain ticket URL that sets the
		// session cookies on its own response instead of embedding them.
		const auto ticket = get(successUrl, false);
		if (ticket.transportOk())
			jar.apply(ticket.setCookies);
	}
	if (!jar.hasSession() && !successUrl.empty()) {
		// Older flow: the cookies are query parameters of the success URL.
		for (const char *name : {"DedeUserID", "DedeUserID__ckMd5", "SESSDATA", "bili_jct"}) {
			const std::string value = queryValue(successUrl, name);
			if (!value.empty())
				jar.set(name, value);
		}
	}
	if (!jar.hasSession())
		return {ErrorKind::Parse, 0, "login succeeded but no session cookies were returned"};

	m_cookies = std::move(jar);
	state = QrState::Confirmed;
	return ApiError::none();
}

ApiError BiliClient::fetchProfile(Profile &profile)
{
	json11::Json json;
	auto error = parseEnvelope(get("https://api.bilibili.com/x/web-interface/nav"), json);
	if (error)
		return error;
	const auto &data = json["data"];
	if (!data["isLogin"].bool_value())
		return {ErrorKind::NotLoggedIn, codes::NotLoggedIn, {}};
	profile.mid = jsonIntOr(data["mid"]);
	profile.name = data["uname"].string_value();
	profile.face = data["face"].string_value();
	if (profile.mid <= 0)
		return {ErrorKind::Parse, 0, "missing mid"};
	return ApiError::none();
}

ApiError BiliClient::fetchRoomId(int64_t mid, int64_t &roomId)
{
	json11::Json json;
	auto error = parseEnvelope(
		get("https://api.live.bilibili.com/room/v2/Room/room_id_by_uid?uid=" + std::to_string(mid)), json);
	if (error)
		return error;
	roomId = jsonIntOr(json["data"]["room_id"]);
	return ApiError::none();
}

ApiError BiliClient::fetchRoomInfo(int64_t roomId, RoomInfo &info)
{
	json11::Json json;
	auto error = parseEnvelope(
		get("https://api.live.bilibili.com/room/v1/Room/get_info?room_id=" + std::to_string(roomId)), json);
	if (error)
		return error;
	const auto &data = json["data"];
	info.roomId = jsonIntOr(data["room_id"], roomId);
	info.title = data["title"].string_value();
	info.areaId = static_cast<int>(jsonIntOr(data["area_id"]));
	info.parentAreaId = static_cast<int>(jsonIntOr(data["parent_area_id"]));
	info.areaName = data["area_name"].string_value();
	info.parentAreaName = data["parent_area_name"].string_value();
	info.live = jsonIntOr(data["live_status"]) == 1;
	info.online = jsonIntOr(data["online"]);
	info.followers = jsonIntOr(data["attention"]);
	info.liveStartedAt = info.live ? parseBeijingTime(data["live_time"].string_value()) : 0;
	return ApiError::none();
}

ApiError BiliClient::fetchRoomNews(int64_t roomId, std::string &content)
{
	json11::Json json;
	auto error = parseEnvelope(
		get("https://api.live.bilibili.com/room_ex/v1/RoomNews/get?roomid=" + std::to_string(roomId)), json);
	if (error)
		return error;
	content = json["data"]["content"].string_value();
	return ApiError::none();
}

ApiError BiliClient::fetchAreas(std::vector<AreaGroup> &groups)
{
	json11::Json json;
	auto error = parseEnvelope(get("https://api.live.bilibili.com/room/v1/Area/getList", false), json);
	if (error)
		return error;
	groups = parseAreaList(json["data"]);
	if (groups.empty())
		return {ErrorKind::Parse, 0, "empty area list"};
	return ApiError::none();
}

ApiError BiliClient::updateRoom(int64_t roomId, const std::optional<std::string> &title, std::optional<int> areaId)
{
	if (!title && !areaId)
		return ApiError::none();
	std::vector<std::pair<std::string, std::string>> fields = {{"room_id", std::to_string(roomId)},
								   {"platform", "pc_link"}};
	if (title)
		fields.emplace_back("title", *title);
	if (areaId)
		fields.emplace_back("area_id", std::to_string(*areaId));
	fields.emplace_back("csrf_token", m_cookies.csrf());
	fields.emplace_back("csrf", m_cookies.csrf());

	json11::Json json;
	return parseEnvelope(post("https://api.live.bilibili.com/room/v1/Room/update", formEncode(fields)), json);
}

ApiError BiliClient::updateNews(int64_t roomId, int64_t mid, const std::string &content)
{
	const std::string form = formEncode({{"room_id", std::to_string(roomId)},
					     {"uid", std::to_string(mid)},
					     {"content", content},
					     {"csrf_token", m_cookies.csrf()},
					     {"csrf", m_cookies.csrf()}});
	json11::Json json;
	return parseEnvelope(post("https://api.live.bilibili.com/xlive/app-blink/v1/index/updateRoomNews", form), json);
}

ApiError BiliClient::startLive(int64_t roomId, int areaId, LiveStart &result)
{
	if (roomId <= 0 || m_cookies.csrf().empty())
		return {ErrorKind::NotLoggedIn, codes::NotLoggedIn, {}};

	// The start endpoint wants the current live-companion build and version.
	json11::Json versionJson;
	const std::string ts = std::to_string(std::time(nullptr));
	auto error = parseEnvelope(
		get("https://api.live.bilibili.com/xlive/app-blink/v1/liveVersionInfo/getHomePageLiveVersion?" +
		    signParams({{"system_version", "2"}, {"ts", ts}})),
		versionJson);
	if (error)
		return error;
	const int64_t build = jsonIntOr(versionJson["data"]["build"]);
	const std::string version = versionJson["data"]["curr_version"].string_value();
	if (build <= 0 || version.empty())
		return {ErrorKind::Parse, 0, "missing live companion version"};

	const std::string form = signParams({{"room_id", std::to_string(roomId)},
					     {"platform", "pc_link"},
					     {"area_v2", std::to_string(areaId)},
					     {"backup_stream", "0"},
					     {"csrf_token", m_cookies.csrf()},
					     {"csrf", m_cookies.csrf()},
					     {"build", std::to_string(build)},
					     {"version", version},
					     {"ts", std::to_string(std::time(nullptr))}});
	json11::Json json;
	error = parseEnvelope(post("https://api.live.bilibili.com/room/v1/Room/startLive", form), json);
	if (error) {
		if (error.code == codes::FaceAuthRequired)
			result.faceAuthUrl = json["data"]["qr"].string_value();
		else if (error.code == codes::FaceAuthRequiredAlt)
			result.faceAuthUrl =
				"https://www.bilibili.com/blackboard/live/face-auth-middle.html?source_event=400&mid=" +
				m_cookies.uid();
		return error;
	}

	result.server = json["data"]["rtmp"]["addr"].string_value();
	result.key = json["data"]["rtmp"]["code"].string_value();
	if (result.server.empty() || result.key.empty())
		return {ErrorKind::Parse, 0, "live started but no RTMP address was returned"};
	return ApiError::none();
}

ApiError BiliClient::stopLive(int64_t roomId)
{
	const std::string form = formEncode({{"room_id", std::to_string(roomId)},
					     {"platform", "pc_link"},
					     {"csrf_token", m_cookies.csrf()},
					     {"csrf", m_cookies.csrf()}});
	json11::Json json;
	return parseEnvelope(post("https://api.live.bilibili.com/room/v1/Room/stopLive", form), json);
}

ApiError BiliClient::logout()
{
	if (m_cookies.csrf().empty())
		return ApiError::none();
	HttpRequest request;
	request.method = "POST";
	request.url = "https://passport.bilibili.com/login/exit/v2";
	request.body = formEncode({{"biliCSRF", m_cookies.csrf()}});
	request.headers = headers(true, true);
	json11::Json json;
	return parseEnvelope(m_http.send(request), json);
}

ApiError BiliClient::fetchBytes(const std::string &url, std::string &bytes)
{
	if (url.rfind("https://", 0) != 0)
		return {ErrorKind::Parse, 0, "refusing non-https url"};
	HttpRequest request;
	request.url = url;
	request.headers = {kUserAgent, "Referer: https://www.bilibili.com/"};
	request.timeoutMs = 8000;
	const auto response = m_http.send(request);
	if (!response.transportOk())
		return networkError(response);
	if (response.status < 200 || response.status >= 300)
		return {ErrorKind::Http, static_cast<int>(response.status), {}};
	if (response.body.size() > 2 * 1024 * 1024)
		return {ErrorKind::Parse, 0, "image too large"};
	bytes = response.body;
	return ApiError::none();
}

} // namespace kaibo
