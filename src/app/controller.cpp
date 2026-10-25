// SPDX-License-Identifier: GPL-2.0-only
#include "app/controller.hpp"
#include "app/curl_transport.hpp"
#include "app/i18n.hpp"
#include "app/worker.hpp"
#include "core/face_auth.hpp"
#include "core/text.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDateTime>

#include <algorithm>
#include <optional>

namespace kaibo {
namespace {

int64_t nowSeconds()
{
	return QDateTime::currentSecsSinceEpoch();
}

QString durationText(int64_t seconds)
{
	const int64_t h = seconds / 3600, m = (seconds / 60) % 60;
	if (h > 0)
		return T("Duration.HoursMinutes").arg(h).arg(m);
	return T("Duration.Minutes").arg(std::max<int64_t>(m, 1));
}

} // namespace

LiveController::LiveController(QObject *parent)
	: QObject(parent),
	  m_transport(std::make_shared<CurlTransport>()),
	  m_client(std::make_shared<BiliClient>(*m_transport))
{
	CurlTransport::globalInit();
	m_watcher = std::make_unique<obs::OutputWatcher>(
		[this](obs::OutputEvent event) { worker::postToUi(this, [this, event] { onOutputEvent(event); }); });

	m_pollTimer.setInterval(30 * 1000);
	connect(&m_pollTimer, &QTimer::timeout, this, &LiveController::refresh);
	m_qrTimer.setInterval(1500);
	connect(&m_qrTimer, &QTimer::timeout, this, &LiveController::pollQr);
	m_faceAuthTimer.setSingleShot(true);
	connect(&m_faceAuthTimer, &QTimer::timeout, this, &LiveController::requestFaceAuthRetry);
}

LiveController::~LiveController()
{
	m_watcher.reset();
}

// ---------------------------------------------------------------- state helpers

LiveController::Phase LiveController::phase() const
{
	const bool roomLive = m_state.room.live;
	if (roomLive && m_state.obsStreaming)
		return m_state.reconnecting ? Phase::Reconnecting : Phase::Live;
	if (roomLive && m_state.dropped)
		return Phase::Dropped;
	if (roomLive)
		return Phase::RoomOnly;
	if (m_state.obsStreaming && m_state.streamTargetIsBilibili && m_state.auth == Auth::SignedIn)
		return Phase::StreamOnly;
	return Phase::Offline;
}

bool LiveController::hasUnsyncedChanges() const
{
	if (!m_state.roomLoaded)
		return false;
	const bool title = !m_state.titleDraft.trimmed().isEmpty() &&
			   m_state.titleDraft.trimmed() != qs(m_state.room.title);
	const bool area = m_state.areaDraft > 0 && m_state.areaDraft != m_state.room.areaId;
	const bool news = m_state.newsLoaded && m_state.newsDraft.trimmed() != qs(m_state.news).trimmed();
	return title || area || news;
}

bool LiveController::titleTooLong() const
{
	return utf8Length(ss(m_state.titleDraft.trimmed())) > static_cast<size_t>(kTitleLimit);
}

QString LiveController::describe(const ApiError &error) const
{
	switch (error.kind) {
	case ErrorKind::None:
		return {};
	case ErrorKind::Network:
		return T("Error.Network");
	case ErrorKind::Http:
		return T("Error.Http").arg(error.code);
	case ErrorKind::Parse:
		return T("Error.Parse");
	case ErrorKind::NotLoggedIn:
		return T("Error.NotLoggedIn");
	case ErrorKind::Api:
		if (error.message.empty())
			return T("Error.Api").arg(error.code);
		return QStringLiteral("%1 (%2)").arg(qs(error.message)).arg(error.code);
	}
	return {};
}

void LiveController::notify(const QString &text, ToastKind kind, const QString &detail)
{
	emit toast(text, static_cast<int>(kind), detail);
}

Settings LiveController::settingsSnapshot() const
{
	Settings s = m_settings;
	s.options = m_state.options;
	s.recentAreas = m_state.recentAreas;
	s.recentTitles = m_state.recentTitles;
	return s;
}

void LiveController::persistSettings()
{
	m_settings = settingsSnapshot();
	m_store.saveSettings(m_settings);
}

// ---------------------------------------------------------------- lifecycle

void LiveController::startup()
{
	m_settings = m_store.loadSettings();
	m_state.options = m_settings.options;
	m_state.recentAreas = m_settings.recentAreas;
	m_state.recentTitles = m_settings.recentTitles;
	m_state.obsStreaming = obs::streamingActive();
	m_state.streamTargetIsBilibili = obs::streamTargetIsBilibili();
	if (m_state.obsStreaming)
		m_watcher->attach();

	const CookieJar jar = m_store.loadCredentials();
	if (jar.hasSession()) {
		validateSession(jar, false);
	} else {
		m_state.auth = Auth::SignedOut;
		m_state.legacyAvailable = m_settings.legacyImport == LegacyImport::Pending &&
					  m_store.findLegacyCredentials().hasSession();
		emit changed();
	}
	m_pollTimer.start();
	obs_log(LOG_INFO, "started; data directory %s", m_store.directory().c_str());
}

void LiveController::onStreamingStarting()
{
	m_watcher->attach();
}

void LiveController::onStreamingStarted()
{
	m_state.obsStreaming = true;
	m_state.dropped = false;
	m_state.reconnecting = false;
	m_state.streamTargetIsBilibili = obs::streamTargetIsBilibili();
	emit changed();
}

void LiveController::onStreamingStopped()
{
	m_state.obsStreaming = false;
	m_state.reconnecting = false;
	if (m_pendingRestore) {
		m_pendingRestore = false;
		std::string error;
		if (obs::restoreStreamService(m_settings.serviceBackup, error)) {
			m_settings.serviceBackup = {};
			persistSettings();
		} else {
			obs_log(LOG_WARNING, "could not restore stream settings: %s", error.c_str());
		}
	}
	emit changed();
}

void LiveController::onOutputEvent(obs::OutputEvent event)
{
	switch (event) {
	case obs::OutputEvent::Reconnecting:
		m_state.reconnecting = true;
		break;
	case obs::OutputEvent::Reconnected:
		m_state.reconnecting = false;
		break;
	case obs::OutputEvent::Dropped:
		m_state.obsStreaming = false;
		m_state.reconnecting = false;
		if (m_state.room.live && !m_ending) {
			m_state.dropped = true;
			notify(T("Msg.StreamDropped"), ToastKind::Error);
		}
		break;
	case obs::OutputEvent::Stopped:
		m_state.obsStreaming = false;
		m_state.reconnecting = false;
		m_state.dropped = false;
		// A deliberate stop in OBS ends the broadcast too, if the person wants that.
		if (!m_ending && m_state.room.live && m_state.auth == Auth::SignedIn &&
		    m_state.options.closeRoomOnStreamStop && m_state.busy == Busy::None)
			closeRoom();
		break;
	}
	emit changed();
}

void LiveController::shutdownForExit()
{
	m_pollTimer.stop();
	m_qrTimer.stop();
	clearFaceAuth();
	m_watcher->detach();
	const bool closingRoom = m_state.options.closeRoomOnExit && m_state.room.live &&
				 m_state.auth == Auth::SignedIn && m_state.roomId > 0;
	if (closingRoom) {
		// OBS is closing: there is no event loop left to wait on, so close the room
		// synchronously with a private client and a short timeout.
		CurlTransport transport;
		BiliClient client(transport);
		client.setCookies(m_cookies);
		client.setTimeoutMs(4000); // OBS is waiting on us to quit
		const ApiError error = client.stopLive(m_state.roomId);
		if (error)
			obs_log(LOG_WARNING, "could not close the live room on exit: %s", error.message.c_str());
		else
			obs_log(LOG_INFO, "closed the live room on exit");
	}
	// Put the user's own stream service back before OBS saves its config on quit: the
	// output is already stopped here, so this cannot cut a running stream.
	if ((!m_state.room.live || closingRoom) && m_settings.serviceBackup.valid()) {
		std::string error;
		if (obs::restoreStreamService(m_settings.serviceBackup, error)) {
			m_settings.serviceBackup = {};
			m_store.saveSettings(m_settings);
			obs_log(LOG_INFO, "restored the previous stream settings on exit");
		} else {
			obs_log(LOG_WARNING, "could not restore stream settings on exit: %s", error.c_str());
		}
	}
}

// ---------------------------------------------------------------- sign-in

void LiveController::validateSession(const CookieJar &jar, bool fromLogin)
{
	struct Result {
		ApiError error;
		Profile profile;
		int64_t roomId = 0;
		RoomInfo room;
		bool roomOk = false;
		std::string news;
		bool newsOk = false;
		CookieJar cookies; // the jar after the requests, so new cookies are not lost
	};
	auto result = std::make_shared<Result>();
	auto client = m_client;
	const int generation = ++m_sessionGeneration;

	m_cookies = jar;
	if (m_state.auth != Auth::SignedIn) // a background re-check keeps the dock as it is
		m_state.auth = Auth::Checking;
	emit changed();

	worker::run(
		this,
		[client, jar, result, now = nowSeconds()] {
			client->setCookies(jar);
			// Bilibili's live APIs expect a stable device identity (buvid3/buvid4) on
			// every request. Mint it once, keep it, and never fail the sign-in over it.
			if (client->ensureDeviceCookies(now))
				obs_log(LOG_WARNING, "could not fetch the Bilibili device id; continuing");
			result->error = client->fetchProfile(result->profile);
			if (!result->error && !client->fetchRoomId(result->profile.mid, result->roomId) &&
			    result->roomId > 0) {
				result->roomOk = !client->fetchRoomInfo(result->roomId, result->room);
				result->newsOk = !client->fetchRoomNews(result->roomId, result->news);
			}
			result->cookies = client->cookies();
		},
		[this, result, jar, fromLogin, generation] {
			if (generation != m_sessionGeneration)
				return;
			if (result->error.kind == ErrorKind::NotLoggedIn) {
				resetSession();
				m_store.clearCredentials();
				notify(T(fromLogin ? "Error.NoCookies" : "Msg.LoginExpired"), ToastKind::Error);
				emit changed();
				return;
			}
			m_state.auth = Auth::SignedIn;
			m_state.legacyAvailable = false;
			// Keep whatever the server handed out (a freshly minted device id, a
			// rotated session cookie) even when the follow-up request failed.
			const CookieJar &fresh = result->cookies.hasSession() ? result->cookies : jar;
			m_cookies = fresh;
			// Do not rewrite the encrypted file on every offline retry: save on success,
			// and on failure only when the server actually changed something.
			if (!result->error || fresh.header() != jar.header())
				m_store.saveCredentials(fresh);
			if (result->error) {
				// Keep the session: most likely offline. The poll timer retries.
				m_state.offline = result->error.kind == ErrorKind::Network;
				if (!m_state.offline)
					notify(describe(result->error), ToastKind::Error);
				emit changed();
				return;
			}
			m_state.offline = false;
			m_state.profile = result->profile;
			m_state.roomId = result->roomId;
			if (result->roomOk)
				applyRoom(result->room);
			if (result->newsOk) {
				m_state.news = result->news;
				m_state.newsDraft = qs(result->news);
				m_state.newsLoaded = true;
			}
			if (fromLogin)
				notify(T("Msg.LoginOk"), ToastKind::Ok);
			loadAvatar();
			ensureAreas();
			emit changed();
		});
}

void LiveController::resetSession()
{
	m_faceAuthTimer.stop();
	m_faceAuthAttempt = 0;
	const Options options = m_state.options;
	const auto recentAreas = m_state.recentAreas;
	const auto recentTitles = m_state.recentTitles;
	const bool streaming = m_state.obsStreaming;
	const bool target = m_state.streamTargetIsBilibili;
	auto areas = std::move(m_state.areas);

	m_state = State();
	m_state.auth = Auth::SignedOut;
	m_state.options = options;
	m_state.recentAreas = recentAreas;
	m_state.recentTitles = recentTitles;
	m_state.obsStreaming = streaming;
	m_state.streamTargetIsBilibili = target;
	m_state.areas = std::move(areas);
	m_cookies.clear();
	++m_sessionGeneration;
	auto client = m_client;
	worker::run(this, [client] { client->setCookies({}); });
	emit avatarChanged(); // never show the previous account's picture
}

void LiveController::startQrLogin()
{
	if (m_state.auth == Auth::SignedIn || m_state.qr == QrPhase::Loading)
		return;
	m_qrTimer.stop();
	m_qrInFlight = false;
	m_state.qr = QrPhase::Loading;
	m_state.qrUrl.clear();
	m_state.qrError.clear();
	emit qrChanged();

	auto client = m_client;
	auto ticket = std::make_shared<QrTicket>();
	auto error = std::make_shared<ApiError>();
	worker::run(
		this, [client, ticket, error] { *error = client->requestQr(*ticket); },
		[this, ticket, error] {
			if (*error) {
				m_state.qr = QrPhase::Failed;
				m_state.qrError = describe(*error);
			} else {
				m_state.qr = QrPhase::Waiting;
				m_state.qrUrl = qs(ticket->url);
				m_qrKey = ticket->key;
				m_qrAge.start();
				m_qrTimer.start();
			}
			emit qrChanged();
		});
}

void LiveController::stopQrLogin()
{
	m_qrTimer.stop();
	if (m_state.qr != QrPhase::Idle) {
		m_state.qr = QrPhase::Idle;
		emit qrChanged();
	}
}

void LiveController::pollQr()
{
	if (m_qrInFlight || m_qrKey.empty())
		return;
	if (m_qrAge.elapsed() > 180 * 1000) {
		m_qrTimer.stop();
		m_state.qr = QrPhase::Expired;
		emit qrChanged();
		return;
	}
	m_qrInFlight = true;
	auto client = m_client;
	const std::string key = m_qrKey;
	struct Result {
		ApiError error;
		QrState state = QrState::Waiting;
		CookieJar jar;
	};
	auto result = std::make_shared<Result>();
	worker::run(
		this,
		[client, key, result] {
			result->error = client->pollQr(key, result->state);
			if (!result->error && result->state == QrState::Confirmed)
				result->jar = client->cookies();
		},
		[this, key, result] {
			m_qrInFlight = false;
			if (key != m_qrKey || m_state.qr == QrPhase::Idle)
				return; // a newer code replaced this one
			if (result->error) {
				// Transient errors: keep polling until the code expires.
				if (result->error.kind != ErrorKind::Network)
					obs_log(LOG_WARNING, "QR poll error %d", result->error.code);
				return;
			}
			switch (result->state) {
			case QrState::Waiting:
				m_state.qr = QrPhase::Waiting;
				break;
			case QrState::Scanned:
				m_state.qr = QrPhase::Scanned;
				break;
			case QrState::Expired:
				m_qrTimer.stop();
				m_state.qr = QrPhase::Expired;
				break;
			case QrState::Confirmed:
				m_qrTimer.stop();
				m_qrKey.clear();
				m_state.qr = QrPhase::Idle;
				emit qrChanged();
				validateSession(result->jar, true);
				return;
			}
			emit qrChanged();
		});
}

void LiveController::importLegacy()
{
	const CookieJar jar = m_store.findLegacyCredentials();
	m_settings.legacyImport = LegacyImport::Done;
	persistSettings();
	m_state.legacyAvailable = false;
	if (jar.hasSession())
		validateSession(jar, true);
	else
		emit changed();
}

void LiveController::dismissLegacy()
{
	m_settings.legacyImport = LegacyImport::Dismissed;
	persistSettings();
	m_state.legacyAvailable = false;
	emit changed();
}

void LiveController::signOut()
{
	auto client = m_client;
	// Invalidate the session on Bilibili's side too (best effort), then forget it locally.
	worker::run(this, [client] {
		if (client->logout())
			obs_log(LOG_INFO, "remote sign-out failed; local session removed anyway");
	});
	m_store.clearCredentials();
	resetSession();
	emit changed();
}

bool LiveController::deleteAllData(QString &error)
{
	signOut();
	m_settings = Settings();
	m_settings.legacyImport = LegacyImport::Dismissed;
	m_state.options = Options();
	m_state.recentAreas.clear();
	m_state.recentTitles.clear();
	std::string message;
	const bool ok = m_store.deleteAll(message);
	error = qs(message);
	emit changed();
	return ok;
}

// ---------------------------------------------------------------- room

void LiveController::applyRoom(const RoomInfo &room)
{
	const bool wasLive = m_state.room.live;
	const QString oldTitle = qs(m_state.room.title);
	const int oldArea = m_state.room.areaId;

	m_state.room = room;
	m_state.roomLoaded = true;
	// The broadcast may have been started from the web live centre or the app right after
	// the verification; there is nothing left to verify here.
	if (room.live)
		clearFaceAuth();
	// Follow the server for fields the person has not edited.
	if (m_state.titleDraft.isEmpty() || m_state.titleDraft == oldTitle)
		m_state.titleDraft = qs(room.title);
	if (m_state.areaDraft == 0 || m_state.areaDraft == oldArea)
		m_state.areaDraft = room.areaId;

	if (room.live && m_state.liveSince == 0)
		m_state.liveSince = room.liveStartedAt > 0 ? room.liveStartedAt : nowSeconds();
	if (!room.live) {
		if (wasLive && m_state.busy == Busy::None && !m_ending)
			notify(T("Msg.RoomClosedElsewhere"), ToastKind::Info);
		m_state.liveSince = 0;
		m_state.dropped = false;
	}
}

void LiveController::refresh()
{
	if (m_state.auth != Auth::SignedIn || m_refreshInFlight || m_state.busy != Busy::None)
		return;
	if (m_state.profile.mid == 0) {
		validateSession(m_cookies, false); // never got a profile (offline at startup)
		return;
	}
	if (m_state.roomId <= 0)
		return;
	m_refreshInFlight = true;
	auto client = m_client;
	const int64_t roomId = m_state.roomId;
	const int generation = m_sessionGeneration;
	auto room = std::make_shared<RoomInfo>();
	auto error = std::make_shared<ApiError>();
	worker::run(
		this, [client, roomId, room, error] { *error = client->fetchRoomInfo(roomId, *room); },
		[this, room, error, generation] {
			m_refreshInFlight = false;
			if (generation != m_sessionGeneration || m_state.busy != Busy::None)
				return;
			if (error->kind == ErrorKind::NotLoggedIn) {
				resetSession();
				m_store.clearCredentials();
				notify(T("Msg.LoginExpired"), ToastKind::Error);
			} else if (*error) {
				m_state.offline = error->kind == ErrorKind::Network;
			} else {
				m_state.offline = false;
				applyRoom(*room);
			}
			emit changed();
		});
}

void LiveController::ensureAreas()
{
	if (!m_state.areas.empty() || m_state.areasLoading)
		return;
	m_state.areasLoading = true;
	auto client = m_client;
	auto groups = std::make_shared<std::vector<AreaGroup>>();
	auto error = std::make_shared<ApiError>();
	worker::run(
		this, [client, groups, error] { *error = client->fetchAreas(*groups); },
		[this, groups, error] {
			m_state.areasLoading = false;
			if (*error)
				obs_log(LOG_WARNING, "area list unavailable: %s", error->message.c_str());
			else
				m_state.areas = std::move(*groups);
			emit changed();
		});
}

void LiveController::loadAvatar()
{
	if (m_state.profile.face.empty())
		return;
	auto client = m_client;
	// Ask Bilibili's image service for a small PNG: Qt decodes PNG without plugins.
	const std::string url = m_state.profile.face + "@96w_96h_1c.png";
	auto bytes = std::make_shared<std::string>();
	auto error = std::make_shared<ApiError>();
	worker::run(
		this, [client, url, bytes, error] { *error = client->fetchBytes(url, *bytes); },
		[this, bytes, error] {
			if (*error)
				return;
			QImage image;
			if (image.loadFromData(reinterpret_cast<const uchar *>(bytes->data()),
					       static_cast<int>(bytes->size()))) {
				m_state.avatar = image;
				emit avatarChanged();
			}
		});
}

void LiveController::setTitleDraft(const QString &title)
{
	m_state.titleDraft = title;
	emit changed();
}

void LiveController::setAreaDraft(int areaId)
{
	m_state.areaDraft = areaId;
	touchRecent(m_state.recentAreas, areaId);
	persistSettings();
	emit changed();
}

void LiveController::setNewsDraft(const QString &news)
{
	m_state.newsDraft = news;
	emit changed();
}

void LiveController::syncRoom()
{
	if (m_state.auth != Auth::SignedIn || m_state.busy != Busy::None || m_state.roomId <= 0 ||
	    !hasUnsyncedChanges())
		return;
	if (titleTooLong()) {
		notify(T("Msg.TitleTooLong").arg(kTitleLimit), ToastKind::Error);
		return;
	}

	std::optional<std::string> title;
	std::optional<int> area;
	std::optional<std::string> news;
	const QString t = m_state.titleDraft.trimmed();
	if (!t.isEmpty() && t != qs(m_state.room.title))
		title = ss(t);
	if (m_state.areaDraft > 0 && m_state.areaDraft != m_state.room.areaId)
		area = m_state.areaDraft;
	if (m_state.newsLoaded && m_state.newsDraft.trimmed() != qs(m_state.news).trimmed())
		news = ss(m_state.newsDraft.trimmed());

	m_state.busy = Busy::Syncing;
	emit changed();

	auto client = m_client;
	const int64_t roomId = m_state.roomId, mid = m_state.profile.mid;
	auto error = std::make_shared<ApiError>();
	worker::run(
		this,
		[client, roomId, mid, title, area, news, error] {
			*error = client->updateRoom(roomId, title, area);
			if (!*error && news)
				*error = client->updateNews(roomId, mid, *news);
		},
		[this, title, area, news, error] {
			m_state.busy = Busy::None;
			if (*error) {
				notify(T("Error.UpdateRoom"), ToastKind::Error, describe(*error));
			} else {
				if (title) {
					m_state.room.title = *title;
					rememberTitle(m_state.recentTitles, *title);
				}
				if (area) {
					m_state.room.areaId = *area;
					if (const Area *a = findArea(m_state.areas, *area)) {
						m_state.room.areaName = a->name;
						m_state.room.parentAreaId = a->parentId;
						m_state.room.parentAreaName = a->parentName;
					}
				}
				if (news)
					m_state.news = *news;
				persistSettings();
				notify(T("Msg.RoomSaved"), ToastKind::Ok);
			}
			emit changed();
		});
}

// ---------------------------------------------------------------- live

void LiveController::goLive()
{
	if (m_state.auth != Auth::SignedIn || m_state.busy != Busy::None)
		return;
	if (m_state.face != FaceAuth::None) {
		// Already waiting on a verification; the card on the main page drives it.
		return;
	}
	if (m_state.roomId <= 0) {
		notify(T("Msg.NoRoom"), ToastKind::Error);
		return;
	}
	if (m_state.areaDraft <= 0) {
		notify(T("Msg.PickArea"), ToastKind::Error);
		return;
	}
	if (titleTooLong()) {
		notify(T("Msg.TitleTooLong").arg(kTitleLimit), ToastKind::Error);
		return;
	}

	std::optional<std::string> title;
	std::optional<int> area;
	const QString t = m_state.titleDraft.trimmed();
	if (!t.isEmpty() && t != qs(m_state.room.title))
		title = ss(t);
	if (m_state.areaDraft != m_state.room.areaId)
		area = m_state.areaDraft;

	m_state.busy = Busy::GoingLive;
	emit changed();

	struct Result {
		ApiError syncError;
		ApiError error;
		LiveStart start;
	};
	auto result = std::make_shared<Result>();
	auto client = m_client;
	const int64_t roomId = m_state.roomId;
	const int areaId = m_state.areaDraft;
	const int generation = m_sessionGeneration;
	worker::run(
		this,
		[client, roomId, areaId, title, area, result] {
			// Title/area first, so viewers never see the old ones on the new broadcast.
			result->syncError = client->updateRoom(roomId, title, area);
			if (result->syncError)
				return;
			result->error = client->startLive(roomId, areaId, result->start);
		},
		[this, result, title, area, generation] {
			if (generation != m_sessionGeneration)
				return;
			m_state.busy = Busy::None;
			if (result->syncError) {
				notify(T("Error.UpdateRoom"), ToastKind::Error, describe(result->syncError));
				emit changed();
				return;
			}
			if (title) {
				m_state.room.title = *title;
				rememberTitle(m_state.recentTitles, *title);
			}
			if (area)
				m_state.room.areaId = *area;
			handleStartLiveResult(result->error, result->start);
		});
}

// Bilibili answered the start request with "face verification required" (60024/60043):
// show the code and wait for the person instead of failing the whole go-live.
void LiveController::handleStartLiveResult(const ApiError &error, const LiveStart &start)
{
	if (!error) {
		finishLiveStart(start);
		return;
	}
	if (!start.faceAuthUrl.empty()) {
		m_faceAuthAttempt = 0;
		m_state.face = FaceAuth::Required;
		m_state.faceAuthUrl = qs(start.faceAuthUrl);
		m_state.faceAuthAttempt = 0;
		notify(T("Msg.FaceAuthNeeded"), ToastKind::Info);
	} else {
		notify(T("Error.StartLive"), ToastKind::Error, describe(error));
	}
	emit changed();
}

void LiveController::confirmFaceAuth()
{
	if (m_state.auth != Auth::SignedIn || m_state.face == FaceAuth::None || m_state.busy != Busy::None)
		return;
	m_faceAuthAttempt = 0;
	m_state.face = FaceAuth::Retrying;
	m_state.faceAuthAttempt = 0;
	m_state.busy = Busy::GoingLive;
	notify(T("Msg.FaceAuthChecking"), ToastKind::Info);
	emit changed();
	retryFaceAuth();
}

void LiveController::cancelFaceAuth()
{
	if (m_state.face == FaceAuth::Retrying)
		return; // a retry is already in flight; let it finish or fail
	clearFaceAuth();
	notify(T("Msg.FaceAuthSkipped"), ToastKind::Info);
	emit changed();
}

void LiveController::clearFaceAuth()
{
	m_faceAuthTimer.stop();
	m_faceAuthAttempt = 0;
	if (m_state.face == FaceAuth::Retrying && m_state.busy == Busy::GoingLive)
		m_state.busy = Busy::None; // the retry loop owned the busy flag
	m_state.face = FaceAuth::None;
	m_state.faceAuthUrl.clear();
	m_state.faceAuthAttempt = 0;
}

// Schedule one more start-live attempt; spaced out because the phone-side verification
// reaches Bilibili's live API a moment after the person finishes it.
void LiveController::retryFaceAuth()
{
	if (m_state.face != FaceAuth::Retrying)
		return; // cancelled, or the room turned out to be live already
	if (m_state.auth != Auth::SignedIn || m_faceAuthAttempt >= kFaceAuthMaxRetries) {
		const int attempts = m_faceAuthAttempt;
		m_state.busy = Busy::None;
		clearFaceAuth();
		notify(T("Error.FaceAuthPending").arg(attempts), ToastKind::Error);
		emit changed();
		return;
	}
	const int delay = faceAuthBackoffMs(m_faceAuthAttempt);
	++m_faceAuthAttempt;
	m_state.faceAuthAttempt = m_faceAuthAttempt;
	m_faceAuthTimer.start(delay);
	emit changed();
}

void LiveController::requestFaceAuthRetry()
{
	if (m_state.auth != Auth::SignedIn || m_state.roomId <= 0) {
		m_state.busy = Busy::None;
		clearFaceAuth();
		emit changed();
		return;
	}
	struct Result {
		ApiError error;
		LiveStart start;
	};
	auto result = std::make_shared<Result>();
	auto client = m_client;
	const int64_t roomId = m_state.roomId;
	const int areaId = m_state.areaDraft;
	const int generation = m_sessionGeneration;
	worker::run(
		this,
		[client, roomId, areaId, result] { result->error = client->startLive(roomId, areaId, result->start); },
		[this, result, generation] {
			if (generation != m_sessionGeneration)
				return;
			if (m_state.face == FaceAuth::None) {
				m_state.busy = Busy::None;
				return; // cancelled while the request was in flight
			}
			if (!result->error) {
				m_state.busy = Busy::None;
				finishLiveStart(result->start);
				return;
			}
			if (!result->start.faceAuthUrl.empty()) {
				// Keep the same code on screen; only the message changes.
				retryFaceAuth();
				return;
			}
			m_state.busy = Busy::None;
			clearFaceAuth();
			notify(T("Error.StartLive"), ToastKind::Error, describe(result->error));
			emit changed();
		});
}

void LiveController::finishLiveStart(const LiveStart &start)
{
	clearFaceAuth();
	m_state.server = qs(start.server);
	m_state.key = qs(start.key);
	m_state.room.live = true;
	m_state.liveSince = nowSeconds();
	m_state.dropped = false;

	if (m_state.obsStreaming) {
		// OBS is already pushing somewhere; changing the service now would cut it.
		notify(T("Msg.RoomOpenedStreaming"), ToastKind::Info);
	} else {
		std::string error;
		ServiceBackup *backup = m_state.options.restoreService ? &m_settings.serviceBackup : nullptr;
		if (!obs::applyStreamService(start.server, start.key, backup, error)) {
			obs_log(LOG_WARNING, "could not write stream settings: %s", error.c_str());
			notify(T("Error.ApplyService"), ToastKind::Error);
		} else {
			m_state.streamTargetIsBilibili = true;
			if (m_state.options.autoStartStream) {
				obs::startStreaming();
				notify(T("Msg.LiveStarted"), ToastKind::Ok);
			} else {
				notify(T("Msg.RoomOpened"), ToastKind::Ok);
			}
		}
	}
	persistSettings();
	emit changed();
}

void LiveController::endLive()
{
	if (m_state.auth != Auth::SignedIn || m_state.busy != Busy::None)
		return;
	m_ending = true;
	if (m_state.obsStreaming)
		obs::stopStreaming();
	closeRoom();
}

void LiveController::closeRoom()
{
	if (m_state.roomId <= 0 || m_state.busy != Busy::None) {
		m_ending = false;
		return;
	}
	m_state.busy = Busy::Ending;
	emit changed();

	auto client = m_client;
	const int64_t roomId = m_state.roomId;
	auto error = std::make_shared<ApiError>();
	worker::run(
		this, [client, roomId, error] { *error = client->stopLive(roomId); },
		[this, error] {
			m_state.busy = Busy::None;
			if (*error) {
				m_ending = false;
				notify(T("Error.StopLive"), ToastKind::Error, describe(*error));
				emit changed();
				return;
			}
			finishRoomClosed(m_state.options.restoreService);
		});
}

void LiveController::finishRoomClosed(bool restoreAfter)
{
	const int64_t duration = m_state.liveSince > 0 ? nowSeconds() - m_state.liveSince : 0;
	m_state.room.live = false;
	m_state.liveSince = 0;
	m_state.dropped = false;
	m_state.key.clear();
	m_state.server.clear();
	m_ending = false;

	if (restoreAfter && m_settings.serviceBackup.valid()) {
		if (m_state.obsStreaming) {
			m_pendingRestore = true; // after OBS has finished stopping
		} else {
			std::string error;
			if (obs::restoreStreamService(m_settings.serviceBackup, error))
				m_settings.serviceBackup = {};
			else
				obs_log(LOG_WARNING, "could not restore stream settings: %s", error.c_str());
		}
	}
	persistSettings();
	notify(duration > 0 ? T("Msg.LiveEnded").arg(durationText(duration)) : T("Msg.RoomClosed"), ToastKind::Info);
	emit changed();
}

void LiveController::startObsStream()
{
	m_state.dropped = false;
	obs::startStreaming();
	emit changed();
}

void LiveController::stopObsStream()
{
	obs::stopStreaming();
}

void LiveController::setOptions(const Options &options)
{
	m_state.options = options;
	// The captured service belongs to the live session, not to the checkbox: while a
	// room is live, toggling the option off and on again must not throw it away.
	if (!options.restoreService && !m_state.room.live)
		m_settings.serviceBackup = {};
	persistSettings();
	emit changed();
}

} // namespace kaibo
