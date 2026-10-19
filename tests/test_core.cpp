// SPDX-License-Identifier: GPL-2.0-only
// Unit tests for the OBS-independent core. Build: see tests/CMakeLists.txt.
#include "app/secret_store.hpp"
#include "core/areas.hpp"
#include "core/bili_client.hpp"
#include "core/cookies.hpp"
#include "core/md5.hpp"
#include "core/settings_model.hpp"
#include "core/text.hpp"
#include "vendor/json11/json11.hpp"

#include <cstdio>
#include <deque>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                    \
	do {                                                                           \
		++g_checks;                                                            \
		if (!(cond)) {                                                         \
			++g_failures;                                                  \
			std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		}                                                                      \
	} while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

struct TestCase {
	const char *name;
	std::function<void()> body;
};

std::vector<TestCase> &registry()
{
	static std::vector<TestCase> tests;
	return tests;
}

struct Register {
	Register(const char *name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

#define TEST(name)                                  \
	static void name();                         \
	static Register register_##name(#name, name); \
	static void name()

// Scripted transport: returns queued responses and records requests.
class FakeTransport : public kaibo::HttpTransport {
public:
	std::deque<kaibo::HttpResponse> responses;
	std::vector<kaibo::HttpRequest> requests;

	void push(long status, const std::string &body, std::vector<std::string> cookies = {})
	{
		kaibo::HttpResponse r;
		r.status = status;
		r.body = body;
		r.setCookies = std::move(cookies);
		responses.push_back(std::move(r));
	}
	void pushNetworkError(const std::string &message)
	{
		kaibo::HttpResponse r;
		r.transportError = message;
		responses.push_back(std::move(r));
	}
	kaibo::HttpResponse send(const kaibo::HttpRequest &request) override
	{
		requests.push_back(request);
		if (responses.empty()) {
			kaibo::HttpResponse r;
			r.transportError = "no scripted response";
			return r;
		}
		auto r = responses.front();
		responses.pop_front();
		return r;
	}
};

bool hasHeader(const kaibo::HttpRequest &request, const std::string &prefix)
{
	for (const auto &h : request.headers)
		if (h.rfind(prefix, 0) == 0)
			return true;
	return false;
}

} // namespace

using namespace kaibo;

TEST(md5_known_vectors)
{
	CHECK_EQ(md5Hex(""), "d41d8cd98f00b204e9800998ecf8427e");
	CHECK_EQ(md5Hex("abc"), "900150983cd24fb0d6963f7d28e17f72");
	CHECK_EQ(md5Hex("The quick brown fox jumps over the lazy dog"), "9e107d9d372bb6826bd81d3542a419d6");
	// 56..64 byte inputs exercise the two-block padding path.
	CHECK_EQ(md5Hex(std::string(56, 'a')), "3b0c8ac703f828b04c6c197006d17218");
	CHECK_EQ(md5Hex(std::string(64, 'a')), "014842d480b571495a4a0363793f7367");
	CHECK_EQ(md5Hex(std::string(1000, 'x')).size(), 32u);
}

TEST(url_and_form_encoding)
{
	CHECK_EQ(urlEncode("abc-_.~"), "abc-_.~");
	CHECK_EQ(urlEncode("a b&c=d"), "a%20b%26c%3Dd");
	CHECK_EQ(urlEncode("\xE5\xBC\x80"), "%E5%BC%80"); // 开
	CHECK_EQ(formEncode({{"a", "1"}, {"title", "x y"}}), "a=1&title=x%20y");
	CHECK_EQ(queryValue("https://x/y?a=1&SESSDATA=abc%2C1&c", "SESSDATA"), "abc%2C1");
	CHECK_EQ(queryValue("https://x/y?a=1#b=2", "b"), "");
	CHECK_EQ(queryValue("https://x/y", "a"), "");
}

TEST(app_signature_matches_reference)
{
	// Reference computed independently: sorted params + appkey, md5(query + secret).
	const std::string signedQuery = BiliClient::signParams({{"ts", "1700000000"}, {"system_version", "2"}});
	CHECK_EQ(signedQuery.rfind("appkey=aae92bc66f3edfab&system_version=2&ts=1700000000&sign=", 0), 0u);
	const std::string expected = md5Hex(std::string("appkey=aae92bc66f3edfab&system_version=2&ts=1700000000") +
					    "af125a0d5279fd576c1b4418a3e8276d");
	CHECK_EQ(signedQuery.substr(signedQuery.size() - 32), expected);
}

TEST(utf8_length_and_formatting)
{
	CHECK_EQ(utf8Length("abc"), 3u);
	CHECK_EQ(utf8Length("\xE5\xBC\x80\xE6\x92\xAD"), 2u); // 开播
	CHECK_EQ(utf8Length("\xF0\x9F\x8E\xAE"), 1u);         // 🎮
	CHECK_EQ(utf8Length("\xFF\xFE"), 2u);                 // invalid bytes still counted
	CHECK_EQ(formatCount(1284, UiLocale::SimplifiedChinese), "1,284");
	CHECK_EQ(formatCount(12000, UiLocale::SimplifiedChinese), "1.2\xE4\xB8\x87");
	CHECK_EQ(formatCount(12000, UiLocale::TraditionalChinese), "1.2\xE8\x90\xAC");
	CHECK_EQ(formatCount(30000, UiLocale::SimplifiedChinese), "3\xE4\xB8\x87");
	CHECK_EQ(formatCount(12345, UiLocale::English), "12.3K");
	CHECK_EQ(formatCount(2500000, UiLocale::English), "2.5M");
	CHECK_EQ(formatCount(-5, UiLocale::English), "0");
	CHECK_EQ(formatClock(0), "00:00:00");
	CHECK_EQ(formatClock(5076), "01:24:36");
	CHECK_EQ(formatClock(90000), "25:00:00");
	CHECK(localeFromTag("zh-TW") == UiLocale::TraditionalChinese);
	CHECK(localeFromTag("zh-HK") == UiLocale::TraditionalChinese);
	CHECK(localeFromTag("zh-CN") == UiLocale::SimplifiedChinese);
	CHECK(localeFromTag("en-US") == UiLocale::English);
	CHECK(localeFromTag("ja-JP") == UiLocale::English);
	int64_t v = 0;
	CHECK(parseInt(" 86 ", v) && v == 86);
	CHECK(!parseInt("86a", v));
	CHECK(!parseInt("", v));
}

TEST(cookie_jar_parse_apply_delete)
{
	auto jar = CookieJar::fromHeader("SESSDATA=s1; bili_jct=c1; DedeUserID=42; junk; =x");
	CHECK(jar.hasSession());
	CHECK_EQ(jar.csrf(), "c1");
	CHECK_EQ(jar.uid(), "42");
	jar.apply({"bili_jct=c2", "sid=abc", "SESSDATA=deleted"});
	CHECK_EQ(jar.csrf(), "c2");
	CHECK_EQ(jar.get("SESSDATA"), "");
	CHECK(!jar.hasSession());
	CHECK_EQ(jar.header(), "bili_jct=c2; DedeUserID=42; sid=abc");
}

TEST(beijing_time_parsing)
{
	// 2024-01-01 08:00:00 CST == 2024-01-01 00:00:00 UTC == 1704067200
	CHECK_EQ(BiliClient::parseBeijingTime("2024-01-01 08:00:00"), 1704067200);
	CHECK_EQ(BiliClient::parseBeijingTime("0000-00-00 00:00:00"), 0);
	CHECK_EQ(BiliClient::parseBeijingTime("garbage"), 0);
}

static const char *kAreaJson = R"({"code":0,"data":[
 {"id":2,"name":"网游","list":[{"id":"86","name":"英雄联盟","lock_status":"0"},{"id":"bad","name":"x"},{"id":"92","name":"DOTA2"}]},
 {"id":3,"name":"手游","list":[{"id":35,"name":"王者荣耀","lock_status":1},{"id":"163","name":"英雄联盟手游"}]},
 {"id":"oops","name":"broken","list":[]},
 {"id":9,"name":"虚拟主播","list":[]}
]})";

TEST(area_parsing_and_search)
{
	std::string err;
	const auto json = json11::Json::parse(kAreaJson, err);
	CHECK(err.empty());
	const auto groups = parseAreaList(json["data"]);
	CHECK_EQ(groups.size(), 2u); // broken id and empty group dropped
	CHECK_EQ(groups[0].areas.size(), 2u);
	CHECK(groups[1].areas[0].locked);
	CHECK(!groups[0].areas[0].locked);
	const Area *lol = findArea(groups, 86);
	CHECK(lol && lol->parentName == "网游");
	CHECK(findArea(groups, 999) == nullptr);

	const auto hits = searchAreas(groups, "英雄联盟");
	CHECK_EQ(hits.size(), 2u);
	CHECK_EQ(hits[1].areas[0].id, 163);
	CHECK_EQ(searchAreas(groups, "dota")[0].areas[0].id, 92);  // ASCII case-insensitive
	CHECK_EQ(searchAreas(groups, "手游")[0].areas.size(), 2u); // parent match keeps all children
	CHECK(searchAreas(groups, "zzz").empty());
	CHECK_EQ(searchAreas(groups, "  ").size(), groups.size());

	std::vector<int> recent = {1, 2, 3};
	touchRecent(recent, 2);
	CHECK((recent == std::vector<int>{2, 1, 3}));
	touchRecent(recent, 9, 3);
	CHECK((recent == std::vector<int>{9, 2, 1}));
	touchRecent(recent, 0);
	CHECK_EQ(recent.size(), 3u);
}

TEST(qr_login_flow_with_cross_domain_cookies)
{
	FakeTransport http;
	BiliClient client(http);
	http.push(200, R"({"code":0,"data":{"url":"https://qr/x","qrcode_key":"k1"}})");
	QrTicket ticket;
	CHECK(!client.requestQr(ticket));
	CHECK_EQ(ticket.key, "k1");

	QrState state = QrState::Waiting;
	http.push(200, R"({"code":0,"data":{"code":86090,"message":"scanned"}})");
	CHECK(!client.pollQr("k1", state));
	CHECK(state == QrState::Scanned);

	// Success without cookies on the poll response: the client follows the ticket URL.
	http.push(
		200,
		R"({"code":0,"data":{"code":0,"url":"https://passport.biligame.com/x/passport-login/web/crossDomain?ticket=t"}})");
	http.push(200, "", {"SESSDATA=sess", "bili_jct=csrf", "DedeUserID=7"});
	CHECK(!client.pollQr("k1", state));
	CHECK(state == QrState::Confirmed);
	CHECK_EQ(client.cookies().csrf(), "csrf");
	CHECK_EQ(http.requests.back().url.find("crossDomain") != std::string::npos, true);
	CHECK(!hasHeader(http.requests[1], "Cookie:")); // login requests never send old cookies
}

TEST(qr_login_flow_with_query_cookies_and_expiry)
{
	FakeTransport http;
	BiliClient client(http);
	QrState state = QrState::Waiting;
	http.push(200, R"({"code":0,"data":{"code":86038}})");
	CHECK(!client.pollQr("k", state));
	CHECK(state == QrState::Expired);

	http.push(200, R"({"code":0,"data":{"code":0,"url":"https://x/?DedeUserID=5&SESSDATA=s%2C1&bili_jct=j"}})");
	http.push(200, ""); // ticket URL gives nothing
	CHECK(!client.pollQr("k", state));
	CHECK(state == QrState::Confirmed);
	CHECK_EQ(client.cookies().get("SESSDATA"), "s%2C1");

	http.push(200, R"({"code":0,"data":{"code":0,"url":""}})");
	CHECK(client.pollQr("k", state).kind == ErrorKind::Parse);
}

TEST(error_envelopes_are_classified)
{
	FakeTransport http;
	BiliClient client(http);
	client.setCookies(CookieJar::fromHeader("SESSDATA=s; bili_jct=c; DedeUserID=1"));
	Profile profile;

	http.pushNetworkError("Could not resolve host");
	CHECK(client.fetchProfile(profile).kind == ErrorKind::Network);
	http.push(503, "busy");
	auto e = client.fetchProfile(profile);
	CHECK(e.kind == ErrorKind::Http && e.code == 503);
	http.push(200, "<html>");
	CHECK(client.fetchProfile(profile).kind == ErrorKind::Parse);
	http.push(200, R"({"code":-101,"message":"账号未登录"})");
	CHECK(client.fetchProfile(profile).kind == ErrorKind::NotLoggedIn);
	http.push(200, R"({"code":0,"data":{"isLogin":false}})");
	CHECK(client.fetchProfile(profile).kind == ErrorKind::NotLoggedIn);
	http.push(200, R"({"code":0,"data":{"isLogin":true,"mid":12345,"uname":"夏","face":"https://i0/f.jpg"}})");
	CHECK(!client.fetchProfile(profile));
	CHECK_EQ(profile.mid, 12345);
	CHECK(hasHeader(http.requests.back(), "Cookie: SESSDATA=s"));

	http.push(200, R"({"code":60009,"msg":"直播间已被封禁"})");
	e = client.stopLive(1);
	CHECK(e.kind == ErrorKind::Api && e.code == 60009 && e.message == "直播间已被封禁");
}

TEST(start_live_success_and_face_auth)
{
	FakeTransport http;
	BiliClient client(http);
	client.setCookies(CookieJar::fromHeader("SESSDATA=s; bili_jct=csrf1; DedeUserID=77"));

	http.push(200, R"({"code":0,"data":{"build":9000,"curr_version":"7.10.0"}})");
	http.push(
		200,
		R"({"code":0,"data":{"rtmp":{"addr":"rtmp://live-push.bilivideo.com/live-bvc/","code":"?streamname=live_1&key=abc"}}})");
	LiveStart start;
	CHECK(!client.startLive(100, 86, start));
	CHECK_EQ(start.server, "rtmp://live-push.bilivideo.com/live-bvc/");
	CHECK_EQ(start.key, "?streamname=live_1&key=abc");
	const auto &post = http.requests.back();
	CHECK_EQ(post.method, "POST");
	CHECK(post.body.find("area_v2=86") != std::string::npos);
	CHECK(post.body.find("csrf=csrf1") != std::string::npos);
	CHECK(post.body.find("&sign=") != std::string::npos);

	http.push(200, R"({"code":0,"data":{"build":9000,"curr_version":"7.10.0"}})");
	http.push(200, R"({"code":60024,"message":"需要人脸认证","data":{"qr":"https://face/qr"}})");
	LiveStart face;
	auto e = client.startLive(100, 86, face);
	CHECK(e.code == codes::FaceAuthRequired);
	CHECK_EQ(face.faceAuthUrl, "https://face/qr");

	http.push(200, R"({"code":0,"data":{"build":9000,"curr_version":"7.10.0"}})");
	http.push(200, R"({"code":60043,"message":"需要人脸认证"})");
	LiveStart face2;
	client.startLive(100, 86, face2);
	CHECK(face2.faceAuthUrl.find("mid=77") != std::string::npos);

	BiliClient anonymous(http);
	CHECK(anonymous.startLive(100, 86, face2).kind == ErrorKind::NotLoggedIn);
}

TEST(room_update_encodes_title)
{
	FakeTransport http;
	BiliClient client(http);
	client.setCookies(CookieJar::fromHeader("SESSDATA=s; bili_jct=c"));
	http.push(200, R"({"code":0})");
	CHECK(!client.updateRoom(5, std::string("冲分 & 聊天"), 86));
	const auto &body = http.requests.back().body;
	CHECK(body.find("title=%E5%86%B2%E5%88%86%20%26%20%E8%81%8A%E5%A4%A9") != std::string::npos);
	CHECK(body.find("area_id=86") != std::string::npos);
	const size_t before = http.requests.size();
	CHECK(!client.updateRoom(5, std::nullopt, std::nullopt)); // nothing to do, no request
	CHECK_EQ(http.requests.size(), before);
}

TEST(room_info_parsing)
{
	FakeTransport http;
	BiliClient client(http);
	http.push(200, R"({"code":0,"data":{"room_id":21452505,"title":"今晚冲分","area_id":86,"parent_area_id":"2",
		"area_name":"英雄联盟","parent_area_name":"网游","live_status":1,"online":1284,"attention":12000,
		"live_time":"2024-01-01 08:00:00"}})");
	RoomInfo info;
	CHECK(!client.fetchRoomInfo(21452505, info));
	CHECK(info.live);
	CHECK_EQ(info.parentAreaId, 2);
	CHECK_EQ(info.liveStartedAt, 1704067200);
	CHECK_EQ(info.followers, 12000);

	http.push(200, R"({"code":0,"data":{"live_status":2,"live_time":"0000-00-00 00:00:00"}})");
	RoomInfo round;
	CHECK(!client.fetchRoomInfo(1, round));
	CHECK(!round.live); // 2 = replay rotation, not live
	CHECK_EQ(round.roomId, 1);
}

TEST(fetch_bytes_rejects_plain_http)
{
	FakeTransport http;
	BiliClient client(http);
	std::string bytes;
	CHECK(client.fetchBytes("http://i0.hdslb.com/a.jpg", bytes).kind == ErrorKind::Parse);
	CHECK(http.requests.empty());
}

TEST(settings_round_trip_and_defaults)
{
	Settings s;
	s.options.autoStartStream = false;
	s.options.restoreService = true;
	s.recentAreas = {86, 92};
	s.recentTitles = {"今晚冲分", "聊天"};
	s.serviceBackup = {"rtmp_common", R"({"service":"Twitch","key":"k"})"};
	s.legacyImport = LegacyImport::Dismissed;

	Settings back;
	CHECK(settingsFromJson(settingsToJson(s), back));
	CHECK(!back.options.autoStartStream);
	CHECK(back.options.restoreService);
	CHECK(back.options.closeRoomOnExit); // untouched default survives
	CHECK((back.recentAreas == std::vector<int>{86, 92}));
	CHECK_EQ(back.recentTitles[0], "今晚冲分");
	CHECK_EQ(back.serviceBackup.type, "rtmp_common");
	CHECK_EQ(back.serviceBackup.settingsJson, s.serviceBackup.settingsJson);
	CHECK(back.legacyImport == LegacyImport::Dismissed);

	Settings broken;
	CHECK(!settingsFromJson("not json", broken));
	CHECK(broken.options.confirmEnd);
	Settings partial;
	CHECK(settingsFromJson(R"({"schema":99,"options":{"confirmEnd":false,"future":1},"recentAreas":["86",-1,"x"]})",
			       partial));
	CHECK(!partial.options.confirmEnd);
	CHECK((partial.recentAreas == std::vector<int>{86}));
	CHECK(!partial.serviceBackup.valid());

	std::vector<std::string> titles = {"a", "b"};
	rememberTitle(titles, " b ");
	CHECK((titles == std::vector<std::string>{"b", "a"}));
	rememberTitle(titles, "   ");
	CHECK_EQ(titles.size(), 2u);
}

TEST(atomic_write_keeps_backup)
{
	const std::string dir = "kaibo-test-tmp";
	const std::string path = dir + "/sub/settings.json";
	std::string error;
	CHECK(writeFileAtomic(path, "one", error));
	CHECK(writeFileAtomic(path, "two", error));
	std::string data;
	CHECK(readFile(path, data) && data == "two");
	CHECK(readFile(path + ".bak", data) && data == "one");
	CHECK(!readFile(dir + "/missing.json", data));
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
}

TEST(secret_seal_round_trip)
{
	// Heap churn between seal and open: a dangling entropy buffer would decrypt with different bytes.
	for (int i = 0; i < 50; ++i) {
		const std::string plain = "SESSDATA=" + std::string(static_cast<size_t>(i) * 7 + 1, 'x');
		const std::string sealed = secret::seal(plain);
		std::vector<std::string> churn(32, std::string(20, static_cast<char>('a' + i % 26)));
		std::string opened;
		CHECK(secret::open(sealed, opened));
		CHECK_EQ(opened, plain);
	}
	std::string opened;
	CHECK(!secret::open("", opened));
	CHECK(!secret::open("XXXX\x01"
			    "abc",
			    opened));
	CHECK(secret::open(std::string("KBS1\0plain", 10), opened) && opened == "plain");
}

int main()
{
	for (const auto &test : registry()) {
		const int before = g_failures;
		test.body();
		std::printf("%s %s\n", g_failures == before ? "PASS" : "FAIL", test.name);
	}
	std::printf("\n%d checks, %d failures, %zu tests\n", g_checks, g_failures, registry().size());
	return g_failures == 0 ? 0 : 1;
}
