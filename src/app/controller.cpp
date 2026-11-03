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

const char *errorKindName(ErrorKind kind)
{
	switch (kind) {
	case ErrorKind::None:
		return "none";
	case ErrorKind::Network:
		return "network";
	case ErrorKind::Http:
		return "http";
	case ErrorKind::Parse:
		return "parse";
	case ErrorKind::Api:
		return "api";
	case ErrorKind::NotLoggedIn:
		return "not-logged-in";
	}
	return "unknown";
}

// One line per failed request for docs/troubleshooting.md: operation, endpoint, the
// error class and Bilibili's own code/message. Deliberately no cookies, session data,
// stream keys or URLs: a login URL can carry a ticket and a stream URL its key.
void logApiFailure(const char *operation, const char *endpoint, const ApiError &error)
{
	if (!error)
		return;
	obs_log(LOG_WARNING, "%s failed: endpoint=%s kind=%s code=%d message=\"%s\"", operation, endpoint,
		errorKindName(error.kind), error.code, error.message.empty() ? "-" : error.message.c_str());
}

// The localized message for a preflight result. The literal T() calls keep
// tools/check-locales.py able to see every key this file uses.
QString preflightText(const PreflightIssue &issue)
{
	switch (issue.code) {
	case PreflightCode::Connecting:
		return T("Loading.Text");
	case PreflightCode::NotSignedIn:
		return T("Error.NotLoggedIn");
	case PreflightCode::NoRoom:
		return T("Msg.NoRoom");
	case PreflightCode::PickArea:
		return T("Msg.PickArea");
	case PreflightCode::AreaLocked:
		return T("Area.LockedHint");
	case PreflightCode::TitleTooLong:
		return T("Msg.TitleTooLong").arg(issue.argument);
	case PreflightCode::NewsTooLong:
		return T("Msg.NewsTooLong").arg(issue.argument);
	case PreflightCode::NoTitle:
		return T("Room.TitlePlaceholder");
	case PreflightCode::Offline:
		return T("Msg.Offline");
	case PreflightCode::AreasUnavailable:
		return T("Area.Unavailable");
	case PreflightCode::RoomLive:
		return T("Banner.RoomOnly");
	case PreflightCode::UnsyncedChanges:
		return T("Room.Unsaved");
	}
	return {};
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
	// A burst of state changes (a validated session, a sync plus its toasts, ...) used to
	// rebuild the whole dock once per change. One repaint per event-loop turn is enough:
	// state() is already final when the signal arrives.
	m_changedTimer.setSingleShot(true);
	m_changedTimer.setInterval(0);
	connect(&m_changedTimer, &QTimer::timeout, this, [this] { emit changed(); });
}

LiveController::~LiveController()
{
	m_watcher.reset();
}

// ---------------------------------------------------------------- state helpers

LiveController::Phase LiveController::phase() const
{
	const bool roomLive = m_state.room.live;
	// "Live" means the room is live AND OBS is actually pushing to Bilibili. OBS could
	// be streaming to another destination while the room is open; that is not a working
	// broadcast and must not look like one.
	if (roomLive && m_state.obsStreaming && m_state.streamTargetIsBilibili)
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

bool LiveController::newsTooLong() const
{
	return utf8Length(ss(m_state.newsDraft.trimmed())) > static_cast<size_t>(kNewsLimit);
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

std::vector<LiveController::PreflightItem> LiveController::preflight() const
{
	PreflightInput input;
	input.signedIn = m_state.auth == Auth::SignedIn;
	input.sessionChecking = m_state.auth == Auth::Checking;
	input.offline = m_state.offline;
	input.roomLoaded = m_state.roomLoaded;
	input.roomId = m_state.roomId;
	input.roomLive = m_state.room.live;
	input.areaId = m_state.areaDraft;
	const Area *area = findArea(m_state.areas, m_state.areaDraft);
	input.areaLocked = area && area->locked;
	input.areasLoaded = !m_state.areas.empty();
	input.areasLoading = m_state.areasLoading;
	input.titleLength = utf8Length(ss(m_state.titleDraft.trimmed()));
	input.newsLength = utf8Length(ss(m_state.newsDraft.trimmed()));
	input.newsLoaded = m_state.newsLoaded;
	input.unsyncedChanges = hasUnsyncedChanges();

	std::vector<PreflightItem> items;
	for (const PreflightIssue &issue : computePreflight(input))
		items.push_back({issue.severity == PreflightSeverity::Error, preflightText(issue)});
	return items;
}

bool LiveController::preflightBlocked() const
{
	for (const PreflightItem &item : preflight())
		if (item.blocking)
			return true;
	return false;
}

void LiveController::notify(const QString &text, ToastKind kind, const QString &detail)
{
	emit toast(text, static_cast<int>(kind), detail);
}

void LiveController::updatePreflight()
{
	m_state.preflight = preflight();
}

void LiveController::emitChanged()
{
	updatePreflight();
	if (m_shuttingDown || m_changedTimer.isActive())
		return;
	m_changedTimer.start();
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

bool LiveController::handleSessionExpiry(const ApiError &error)
{
	if (error.kind != ErrorKind::NotLoggedIn)
		return false;
	obs_log(LOG_WARNING, "Bilibili no longer accepts the session (code=%d, message=%s); signing out", error.code,
		error.message.empty() ? "-" : error.message.c_str());
	resetSession();
	m_store.clearCredentials();
	notify(T("Msg.LoginExpired"), ToastKind::Error);
	return true;
}

void LiveController::absorbClientCookies(const CookieJar &jar)
{
	if (m_state.auth != Auth::SignedIn || !jar.hasSession() || jar.header() == m_cookies.header())
		return;
	m_cookies = jar;
	if (m_store.saveCredentials(jar))
		obs_log(LOG_INFO, "saved rotated sign-in cookies");
}

void LiveController::finishBusyOperation()
{
	m_state.busy = Busy::None;
	if (m_shuttingDown || !m_closeRoomPending)
		return;
	m_closeRoomPending = false;
	if (m_state.room.live && m_state.auth == Auth::SignedIn)
		closeRoom();
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
		obs_log(LOG_INFO, "no stored sign-in; showing the QR sign-in");
		emitChanged();
	}
	m_pollTimer.start();
	obs_log(LOG_INFO, "started; data directory %s", m_store.directory().c_str());
}

void LiveController::onStreamingStarting()
{
	m_state.streamStarting = true;
	m_state.streamTargetIsBilibili = obs::streamTargetIsBilibili();
	m_watcher->attach();
	obs_log(LOG_INFO, "OBS is starting the stream (bilibili target=%d)", m_state.streamTargetIsBilibili ? 1 : 0);
	emitChanged();
}

void LiveController::onStreamingStarted()
{
	m_state.obsStreaming = true;
	m_state.streamStarting = false;
	m_state.dropped = false;
	m_state.reconnecting = false;
	m_state.streamTargetIsBilibili = obs::streamTargetIsBilibili();
	obs_log(LOG_INFO, "OBS stream started (bilibili target=%d)", m_state.streamTargetIsBilibili ? 1 : 0);
	emitChanged();
}

void LiveController::onStreamingStopped()
{
	m_state.obsStreaming = false;
	m_state.streamStarting = false;
	m_state.reconnecting = false;
	if (m_state.room.live && !m_ending && m_state.streamTargetIsBilibili)
		obs_log(LOG_INFO, "OBS stopped streaming; the room is still open");
	if (m_pendingRestore) {
		m_pendingRestore = false;
		restorePreviousService();
	}
	emitChanged();
}

void LiveController::onOutputEvent(obs::OutputEvent event)
{
	switch (event) {
	case obs::OutputEvent::Reconnecting:
		m_state.reconnecting = true;
		obs_log(LOG_INFO, "OBS is reconnecting the stream");
		break;
	case obs::OutputEvent::Reconnected:
		m_state.reconnecting = false;
		obs_log(LOG_INFO, "OBS reconnected the stream");
		break;
	case obs::OutputEvent::Dropped:
		m_state.obsStreaming = false;
		m_state.streamStarting = false;
		m_state.reconnecting = false;
		if (m_state.room.live && !m_ending) {
			m_state.dropped = true;
			obs_log(LOG_WARNING, "the stream dropped; the room stays open");
			notify(T("Msg.StreamDropped"), ToastKind::Error);
		}
		break;
	case obs::OutputEvent::Stopped:
		m_state.obsStreaming = false;
		m_state.streamStarting = false;
		m_state.reconnecting = false;
		m_state.dropped = false;
		// A deliberate stop in OBS ends the broadcast too, if the person wants that.
		// Only when the stream that stopped was the Bilibili one: stopping a stream to
		// another platform must not close the Bilibili room.
		if (!m_ending && m_state.room.live && m_state.auth == Auth::SignedIn &&
		    m_state.options.closeRoomOnStreamStop && m_state.busy == Busy::None &&
		    m_state.streamTargetIsBilibili)
			closeRoom();
		break;
	}
	emitChanged();
}

void LiveController::shutdownForExit()
{
	if (m_shuttingDown)
		return;
	m_shuttingDown = true;
	m_changedTimer.stop();
	m_pollTimer.stop();
	m_qrTimer.stop();
	m_faceAuthTimer.stop();
	clearFaceAuth();
	m_watcher->detach();

	// A request may still be in flight. Abort it, let it settle, and take the worker's
	// latest cookies so the close below uses the session that Bilibili currently knows.
	auto latestCookies = std::make_shared<CookieJar>(m_cookies);
	auto client = m_client;
	worker::run(this, [client, latestCookies] { *latestCookies = client->cookies(); });
	worker::abortInFlight();
	if (worker::waitIdle(2000))
		m_cookies = *latestCookies;
	else
		obs_log(LOG_WARNING, "a background request did not finish before exit; closing the room anyway");

	const bool closingRoom = m_state.options.closeRoomOnExit && m_state.room.live &&
				 m_state.auth == Auth::SignedIn && m_state.roomId > 0;
	if (closingRoom) {
		// OBS is closing: there is no event loop left to wait on, so close the room
		// synchronously with a private client and a short timeout.
		CurlTransport transport;
		BiliClient exitClient(transport);
		exitClient.setCookies(m_cookies);
		exitClient.setTimeoutMs(4000); // OBS is waiting on us to quit
		const ApiError error = exitClient.stopLive(m_state.roomId);
		if (error) {
			obs_log(LOG_WARNING, "could not close the live room on exit: kind=%s code=%d message=%s",
				errorKindName(error.kind), error.code,
				error.message.empty() ? "-" : error.message.c_str());
		} else {
			obs_log(LOG_INFO, "closed the live room on exit");
			m_state.room.live = false;
		}
	}
	// Put the person's own stream service back before OBS saves its config on quit: the
	// output is already stopped here, so this cannot cut a running stream.
	if ((!m_state.room.live || closingRoom) && m_settings.serviceBackup.valid())
		restorePreviousService();
}

// ---------------------------------------------------------------- sign-in

void LiveController::validateSession(const CookieJar &jar, bool fromLogin)
{
	if (m_shuttingDown)
		return;
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
	emitChanged();

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
			if (m_shuttingDown)
				return;
			if (generation != m_sessionGeneration)
				return;
			if (result->error.kind == ErrorKind::NotLoggedIn) {
				obs_log(LOG_INFO, "Bilibili rejected the stored session (code=%d, message=%s)",
					result->error.code, result->error.message.c_str());
				resetSession();
				m_store.clearCredentials();
				notify(T(fromLogin ? "Error.NoCookies" : "Msg.LoginExpired"), ToastKind::Error);
				emitChanged();
				return;
			}
			const bool wasSignedIn = m_state.auth == Auth::SignedIn;
			const std::string previousFace = m_state.profile.face;
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
				logApiFailure("validating the sign-in", "nav/room", result->error);
				// Keep the session: most likely offline. The poll timer retries.
				m_state.offline = result->error.kind == ErrorKind::Network;
				if (!m_state.offline)
					notify(describe(result->error), ToastKind::Error);
				emitChanged();
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
			else if (!wasSignedIn)
				obs_log(LOG_INFO, "sign-in valid (mid=%lld, room=%lld)",
					static_cast<long long>(m_state.profile.mid),
					static_cast<long long>(m_state.roomId));
			// Accounts without a room are re-validated periodically: re-downloading the
			// same avatar every 30 s would be pointless churn.
			if (result->profile.face != previousFace || m_state.avatar.isNull())
				loadAvatar();
			ensureAreas();
			emitChanged();
		});
}

void LiveController::resetSession()
{
	m_faceAuthTimer.stop();
	m_qrTimer.stop();
	m_qrKey.clear();
	m_qrInFlight = false;
	++m_qrRequestId;
	m_closeRoomPending = false;
	m_ending = false;
	m_pendingRestore = false;
	m_faceAuthAttempt = 0;
	const Options options = m_state.options;
	const auto recentAreas = m_state.recentAreas;
	const auto recentTitles = m_state.recentTitles;
	const bool streaming = m_state.obsStreaming;
	const bool streamStarting = m_state.streamStarting;
	const bool target = m_state.streamTargetIsBilibili;
	auto areas = std::move(m_state.areas);

	m_state = State();
	m_state.auth = Auth::SignedOut;
	m_state.options = options;
	m_state.recentAreas = recentAreas;
	m_state.recentTitles = recentTitles;
	m_state.obsStreaming = streaming;
	m_state.streamStarting = streamStarting;
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
	if (m_shuttingDown || m_state.auth == Auth::SignedIn || m_state.qr == QrPhase::Loading)
		return;
	m_qrTimer.stop();
	m_qrInFlight = false;
	m_state.qr = QrPhase::Loading;
	m_state.qrUrl.clear();
	m_state.qrError.clear();
	const int request = ++m_qrRequestId;
	emit qrChanged();

	auto client = m_client;
	auto ticket = std::make_shared<QrTicket>();
	auto error = std::make_shared<ApiError>();
	worker::run(
		this, [client, ticket, error] { *error = client->requestQr(*ticket); },
		[this, ticket, error, request] {
			if (m_shuttingDown || request != m_qrRequestId)
				return; // stopped, refreshed or signed in while this was in flight
			if (*error) {
				logApiFailure("getting a sign-in QR code", "qrcode/generate", *error);
				m_state.qr = QrPhase::Failed;
				m_state.qrError = describe(*error);
			} else {
				m_state.qr = QrPhase::Waiting;
				m_state.qrUrl = qs(ticket->url);
				m_qrKey = ticket->key;
				m_qrAge.start();
				m_qrTimer.start();
				obs_log(LOG_INFO, "sign-in QR code shown");
			}
			emit qrChanged();
		});
}

void LiveController::stopQrLogin()
{
	m_qrTimer.stop();
	++m_qrRequestId;
	if (m_state.qr != QrPhase::Idle) {
		m_state.qr = QrPhase::Idle;
		emit qrChanged();
	}
}

void LiveController::pollQr()
{
	if (m_shuttingDown || m_qrInFlight || m_qrKey.empty())
		return;
	if (m_qrAge.elapsed() > 180 * 1000) {
		m_qrTimer.stop();
		m_state.qr = QrPhase::Expired;
		obs_log(LOG_INFO, "sign-in QR code expired");
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
			if (m_shuttingDown)
				return;
			m_qrInFlight = false;
			if (key != m_qrKey || m_state.qr == QrPhase::Idle)
				return; // a newer code replaced this one
			if (result->error) {
				// Transient errors: keep polling until the code expires. Log only
				// non-network failures (a network outage would log every 1.5 s).
				if (result->error.kind != ErrorKind::Network)
					logApiFailure("checking the sign-in QR code", "qrcode/poll", result->error);
				return;
			}
			switch (result->state) {
			case QrState::Waiting:
				m_state.qr = QrPhase::Waiting;
				break;
			case QrState::Scanned:
				if (m_state.qr != QrPhase::Scanned)
					obs_log(LOG_INFO, "sign-in QR code scanned; waiting for confirmation");
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
				obs_log(LOG_INFO, "sign-in QR code confirmed");
				emit qrChanged();
				validateSession(result->jar, true);
				return;
			}
			emit qrChanged();
		});
}

void LiveController::importLegacy()
{
	if (m_shuttingDown)
		return;
	const CookieJar jar = m_store.findLegacyCredentials();
	m_settings.legacyImport = LegacyImport::Done;
	persistSettings();
	m_state.legacyAvailable = false;
	if (jar.hasSession()) {
		obs_log(LOG_INFO, "importing the sign-in of the old BILIBILI Stream for OBS plugin");
		validateSession(jar, true);
	} else {
		emitChanged();
	}
}

void LiveController::dismissLegacy()
{
	m_settings.legacyImport = LegacyImport::Dismissed;
	persistSettings();
	m_state.legacyAvailable = false;
	emitChanged();
}

void LiveController::signOut()
{
	if (m_shuttingDown)
		return;
	auto client = m_client;
	// Invalidate the session on Bilibili's side too (best effort), then forget it locally.
	worker::run(this, [client] {
		if (client->logout())
			obs_log(LOG_INFO, "remote sign-out failed; local session removed anyway");
	});
	m_store.clearCredentials();
	resetSession();
	obs_log(LOG_INFO, "signed out");
	emitChanged();
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
	if (ok)
		obs_log(LOG_INFO, "deleted all Kaibo data");
	else
		obs_log(LOG_WARNING, "some Kaibo files could not be deleted: %s", message.c_str());
	emitChanged();
	return ok;
}

// ---------------------------------------------------------------- room

void LiveController::applyRoom(const RoomInfo &room)
{
	const bool wasLive = m_state.room.live;
	const QString oldTitle = qs(m_state.room.title);
	const int oldArea = m_state.room.areaId;
	const int64_t previousRoomId = m_state.roomId;

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
		if (wasLive && m_state.busy == Busy::None && !m_ending) {
			obs_log(LOG_INFO, "the room was closed elsewhere (web, app or Bilibili)");
			notify(T("Msg.RoomClosedElsewhere"), ToastKind::Info);
		}
		m_state.liveSince = 0;
		m_state.dropped = false;
	}
	if (wasLive != room.live)
		obs_log(LOG_INFO, "room %lld is now %s",
			static_cast<long long>(room.roomId ? room.roomId : previousRoomId),
			room.live ? "live" : "closed");
}

void LiveController::refresh()
{
	if (m_shuttingDown || m_state.auth != Auth::SignedIn || m_refreshInFlight || m_state.busy != Busy::None)
		return;
	if (m_state.profile.mid == 0 || m_state.roomId <= 0) {
		// No profile/room yet: re-validate the session. This also keeps the offline state
		// honest for accounts that never opened a room.
		validateSession(m_cookies, false);
		return;
	}
	m_refreshInFlight = true;
	struct Result {
		ApiError error;
		RoomInfo room;
		bool wantNews = false;
		ApiError newsError;
		std::string news;
	};
	auto result = std::make_shared<Result>();
	result->wantNews = !m_state.newsLoaded;
	auto client = m_client;
	const int64_t roomId = m_state.roomId;
	const int generation = m_sessionGeneration;
	worker::run(
		this,
		[client, roomId, result] {
			result->error = client->fetchRoomInfo(roomId, result->room);
			if (!result->error && result->wantNews)
				result->newsError = client->fetchRoomNews(roomId, result->news);
		},
		[this, result, generation] {
			if (m_shuttingDown)
				return;
			m_refreshInFlight = false;
			if (generation != m_sessionGeneration || m_state.busy != Busy::None)
				return;
			if (result->error.kind == ErrorKind::NotLoggedIn) {
				handleSessionExpiry(result->error);
				emitChanged();
				return;
			}
			if (result->error) {
				const bool wasOffline = m_state.offline;
				m_state.offline = result->error.kind == ErrorKind::Network;
				if (m_state.offline && !wasOffline)
					obs_log(LOG_INFO, "cannot reach Bilibili; retrying every %d s",
						m_pollTimer.interval() / 1000);
				else if (!m_state.offline)
					logApiFailure("refreshing the room", "Room/get_info", result->error);
				emitChanged();
				return;
			}
			if (m_state.offline)
				obs_log(LOG_INFO, "Bilibili is reachable again");
			m_state.offline = false;
			applyRoom(result->room);
			if (result->wantNews) {
				if (!result->newsError) {
					m_state.news = result->news;
					m_state.newsDraft = qs(result->news);
					m_state.newsLoaded = true;
				} else {
					logApiFailure("loading the room announcement", "RoomNews/get",
						      result->newsError);
				}
			}
			ensureAreas();
			emitChanged();
		});
}

void LiveController::ensureAreas()
{
	if (m_shuttingDown || !m_state.areas.empty() || m_state.areasLoading)
		return;
	m_state.areasLoading = true;
	auto client = m_client;
	auto groups = std::make_shared<std::vector<AreaGroup>>();
	auto error = std::make_shared<ApiError>();
	worker::run(
		this, [client, groups, error] { *error = client->fetchAreas(*groups); },
		[this, groups, error] {
			if (m_shuttingDown)
				return;
			m_state.areasLoading = false;
			if (*error) {
				logApiFailure("loading the area list", "Area/getList", *error);
			} else {
				m_state.areas = std::move(*groups);
				obs_log(LOG_INFO, "loaded %zu area groups", m_state.areas.size());
			}
			emitChanged();
		});
}

void LiveController::loadAvatar()
{
	if (m_shuttingDown || m_state.profile.face.empty())
		return;
	auto client = m_client;
	// Ask Bilibili's image service for a small PNG: Qt decodes PNG without plugins.
	const std::string url = m_state.profile.face + "@96w_96h_1c.png";
	const int generation = m_sessionGeneration;
	const int64_t mid = m_state.profile.mid;
	auto bytes = std::make_shared<std::string>();
	auto error = std::make_shared<ApiError>();
	worker::run(
		this, [client, url, bytes, error] { *error = client->fetchBytes(url, *bytes); },
		[this, bytes, error, generation, mid] {
			if (m_shuttingDown || generation != m_sessionGeneration || m_state.profile.mid != mid)
				return; // the account changed while the image was in flight
			if (*error) {
				obs_log(LOG_DEBUG, "avatar image unavailable: %s", error->message.c_str());
				return;
			}
			QImage image;
			if (image.loadFromData(reinterpret_cast<const uchar *>(bytes->data()),
					       static_cast<int>(bytes->size()))) {
				m_state.avatar = image;
				emit avatarChanged();
			} else {
				obs_log(LOG_DEBUG, "avatar image could not be decoded");
			}
		});
}

void LiveController::setTitleDraft(const QString &title)
{
	m_state.titleDraft = title;
	emitChanged();
}

void LiveController::setAreaDraft(int areaId)
{
	m_state.areaDraft = areaId;
	touchRecent(m_state.recentAreas, areaId);
	persistSettings();
	emitChanged();
}

void LiveController::setNewsDraft(const QString &news)
{
	m_state.newsDraft = news;
	emitChanged();
}

void LiveController::syncRoom()
{
	if (m_shuttingDown || m_state.auth != Auth::SignedIn || m_state.busy != Busy::None || m_state.roomId <= 0 ||
	    !hasUnsyncedChanges())
		return;
	if (titleTooLong()) {
		notify(T("Msg.TitleTooLong").arg(kTitleLimit), ToastKind::Error);
		return;
	}
	if (newsTooLong()) {
		notify(T("Msg.NewsTooLong").arg(kNewsLimit), ToastKind::Error);
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
	emitChanged();

	struct Result {
		ApiError roomError;
		ApiError newsError;
		CookieJar cookies;
	};
	auto result = std::make_shared<Result>();
	auto client = m_client;
	const int64_t roomId = m_state.roomId, mid = m_state.profile.mid;
	const int generation = m_sessionGeneration;
	worker::run(
		this,
		[client, roomId, mid, title, area, news, result] {
			result->roomError = client->updateRoom(roomId, title, area);
			if (!result->roomError && news)
				result->newsError = client->updateNews(roomId, mid, *news);
			result->cookies = client->cookies();
		},
		[this, title, area, news, result, generation] {
			if (m_shuttingDown)
				return;
			if (generation != m_sessionGeneration) {
				if (m_state.auth == Auth::SignedIn)
					finishBusyOperation();
				return;
			}
			finishBusyOperation();
			if (handleSessionExpiry(result->roomError) || handleSessionExpiry(result->newsError)) {
				emitChanged();
				return;
			}
			const ApiError &error = result->roomError ? result->roomError : result->newsError;
			if (error) {
				logApiFailure("saving the room info",
					      result->roomError ? "Room/update" : "updateRoomNews", error);
				notify(T("Error.UpdateRoom"), ToastKind::Error, describe(error));
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
				absorbClientCookies(result->cookies);
				persistSettings();
				obs_log(LOG_INFO, "room info saved");
				notify(T("Msg.RoomSaved"), ToastKind::Ok);
			}
			emitChanged();
		});
}

// ---------------------------------------------------------------- live

void LiveController::goLive()
{
	if (m_shuttingDown)
		return;
	if (m_state.auth != Auth::SignedIn || m_state.busy != Busy::None)
		return;
	if (m_state.face != FaceAuth::None) {
		// Already waiting on a verification; the card on the main page drives it.
		return;
	}

	// Local preflight: title/announcement length, area choice and lock state, session and
	// room state. Errors stop here with the actionable message; warnings only log and
	// never block a broadcast the person wants to start.
	const std::vector<PreflightItem> issues = preflight();
	for (const PreflightItem &issue : issues) {
		if (!issue.blocking) {
			obs_log(LOG_INFO, "go-live warning: %s", qPrintable(issue.message));
			continue;
		}
		obs_log(LOG_WARNING, "go-live blocked: %s", qPrintable(issue.message));
		notify(issue.message, ToastKind::Error);
		emitChanged();
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

	m_state.busy = Busy::GoingLive;
	emitChanged();
	obs_log(LOG_INFO, "going live (room=%lld area=%d%s)", static_cast<long long>(m_state.roomId), m_state.areaDraft,
		news ? ", announcement" : "");

	struct Result {
		ApiError syncError;
		ApiError newsError;
		ApiError error;
		LiveStart start;
		CookieJar cookies;
	};
	auto result = std::make_shared<Result>();
	auto client = m_client;
	const int64_t roomId = m_state.roomId;
	const int64_t mid = m_state.profile.mid;
	const int areaId = m_state.areaDraft;
	const int generation = m_sessionGeneration;
	worker::run(
		this,
		[client, roomId, mid, areaId, title, area, news, result] {
			// Title/area first, so viewers never see the old ones on the new broadcast.
			result->syncError = client->updateRoom(roomId, title, area);
			if (result->syncError)
				return;
			// The announcement is secondary: failing to save it must not stop the
			// broadcast, so it does not abort the start below.
			if (news)
				result->newsError = client->updateNews(roomId, mid, *news);
			result->error = client->startLive(roomId, areaId, result->start);
			result->cookies = client->cookies();
		},
		[this, result, title, area, generation] {
			if (m_shuttingDown)
				return;
			if (generation != m_sessionGeneration) {
				if (m_state.auth == Auth::SignedIn)
					finishBusyOperation();
				return;
			}
			finishBusyOperation();
			if (result->syncError) {
				if (handleSessionExpiry(result->syncError)) {
					emitChanged();
					return;
				}
				logApiFailure("updating the room before going live", "Room/update", result->syncError);
				notify(T("Error.UpdateRoom"), ToastKind::Error, describe(result->syncError));
				emitChanged();
				return;
			}
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
			absorbClientCookies(result->cookies);
			if (result->newsError)
				logApiFailure("saving the announcement before going live", "updateRoomNews",
					      result->newsError);
			if (handleSessionExpiry(result->error)) {
				emitChanged();
				return;
			}
			const bool started = !result->error;
			handleStartLiveResult(result->error, result->start);
			if (result->newsError && started) {
				// The broadcast is running but the announcement was not saved; say so
				// instead of leaving a silently unsaved edit behind.
				notify(T("Error.UpdateRoom"), ToastKind::Error, describe(result->newsError));
			}
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
		obs_log(LOG_INFO, "Bilibili asks for a face verification before this broadcast (code=%d)", error.code);
		m_faceAuthAttempt = 0;
		m_state.face = FaceAuth::Required;
		m_state.faceAuthUrl = qs(start.faceAuthUrl);
		m_state.faceAuthAttempt = 0;
		notify(T("Msg.FaceAuthNeeded"), ToastKind::Info);
	} else {
		logApiFailure("going live", "Room/startLive", error);
		notify(T("Error.StartLive"), ToastKind::Error, describe(error));
	}
	emitChanged();
}

void LiveController::confirmFaceAuth()
{
	if (m_shuttingDown || m_state.auth != Auth::SignedIn || m_state.face == FaceAuth::None ||
	    m_state.busy != Busy::None)
		return;
	m_faceAuthAttempt = 0;
	m_state.face = FaceAuth::Retrying;
	m_state.faceAuthAttempt = 0;
	m_state.busy = Busy::GoingLive;
	obs_log(LOG_INFO, "face verification confirmed; retrying go-live");
	notify(T("Msg.FaceAuthChecking"), ToastKind::Info);
	emitChanged();
	retryFaceAuth();
}

void LiveController::cancelFaceAuth()
{
	if (m_state.face == FaceAuth::Retrying)
		return; // a retry is already in flight; let it finish or fail
	clearFaceAuth();
	notify(T("Msg.FaceAuthSkipped"), ToastKind::Info);
	emitChanged();
}

void LiveController::clearFaceAuth()
{
	const bool wasRetrying = m_state.face == FaceAuth::Retrying;
	m_faceAuthTimer.stop();
	m_faceAuthAttempt = 0;
	m_state.face = FaceAuth::None;
	m_state.faceAuthUrl.clear();
	m_state.faceAuthAttempt = 0;
	if (wasRetrying && m_state.busy == Busy::GoingLive)
		finishBusyOperation();
}

// Schedule one more start-live attempt; spaced out because the phone-side verification
// reaches Bilibili's live API a moment after the person finishes it.
void LiveController::retryFaceAuth()
{
	if (m_state.face != FaceAuth::Retrying)
		return; // cancelled, or the room turned out to be live already
	if (m_state.auth != Auth::SignedIn || m_faceAuthAttempt >= kFaceAuthMaxRetries) {
		const int attempts = m_faceAuthAttempt;
		obs_log(LOG_WARNING, "face verification still required after %d retries; giving up", attempts);
		finishBusyOperation();
		clearFaceAuth();
		notify(T("Error.FaceAuthPending").arg(attempts), ToastKind::Error);
		emitChanged();
		return;
	}
	const int delay = faceAuthBackoffMs(m_faceAuthAttempt);
	++m_faceAuthAttempt;
	m_state.faceAuthAttempt = m_faceAuthAttempt;
	obs_log(LOG_INFO, "retrying go-live after face verification (%d/%d in %d ms)", m_faceAuthAttempt,
		kFaceAuthMaxRetries, delay);
	m_faceAuthTimer.start(delay);
	emitChanged();
}

void LiveController::requestFaceAuthRetry()
{
	if (m_shuttingDown)
		return;
	if (m_state.auth != Auth::SignedIn || m_state.roomId <= 0) {
		finishBusyOperation();
		clearFaceAuth();
		emitChanged();
		return;
	}
	struct Result {
		ApiError error;
		LiveStart start;
		CookieJar cookies;
	};
	auto result = std::make_shared<Result>();
	auto client = m_client;
	const int64_t roomId = m_state.roomId;
	const int areaId = m_state.areaDraft;
	const int generation = m_sessionGeneration;
	worker::run(
		this,
		[client, roomId, areaId, result] {
			result->error = client->startLive(roomId, areaId, result->start);
			result->cookies = client->cookies();
		},
		[this, result, generation] {
			if (m_shuttingDown)
				return;
			if (generation != m_sessionGeneration) {
				if (m_state.auth == Auth::SignedIn)
					finishBusyOperation();
				return;
			}
			if (m_state.face == FaceAuth::None) {
				finishBusyOperation();
				return; // cancelled while the request was in flight
			}
			if (handleSessionExpiry(result->error)) {
				finishBusyOperation();
				clearFaceAuth();
				emitChanged();
				return;
			}
			if (!result->error) {
				absorbClientCookies(result->cookies);
				finishBusyOperation();
				finishLiveStart(result->start);
				return;
			}
			if (!result->start.faceAuthUrl.empty()) {
				// Keep the same code on screen; only the message changes.
				retryFaceAuth();
				return;
			}
			logApiFailure("retrying go-live after face verification", "Room/startLive", result->error);
			finishBusyOperation();
			clearFaceAuth();
			notify(T("Error.StartLive"), ToastKind::Error, describe(result->error));
			emitChanged();
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
	m_state.offline = false;

	if (m_state.obsStreaming || m_state.streamStarting) {
		// OBS is already pushing (or connecting): changing the service now would cut it.
		notify(T("Msg.RoomOpenedStreaming"), ToastKind::Info);
	} else {
		// Capture and seal the person's service BEFORE OBS is modified: if writing the
		// sealed file fails, the in-memory copy still restores this session, and a crash
		// can never leave Kaibo's service in OBS with no way back.
		if (m_state.options.restoreService) {
			ServiceBackup captured;
			if (obs::captureStreamService(captured)) {
				m_settings.serviceBackup = captured;
				if (m_store.saveServiceBackup(captured))
					obs_log(LOG_INFO, "saved the previous OBS stream settings for restore");
				else
					obs_log(LOG_WARNING,
						"could not seal the previous stream settings; they can only be restored until OBS exits");
			}
		}
		std::string error;
		if (!obs::applyStreamService(start.server, start.key, error)) {
			obs_log(LOG_WARNING, "could not write the stream settings: %s", error.c_str());
			notify(T("Error.ApplyService"), ToastKind::Error);
		} else {
			m_state.streamTargetIsBilibili = true;
			obs_log(LOG_INFO, "wrote the Bilibili stream settings into OBS");
			if (m_state.options.autoStartStream) {
				obs::startStreaming();
				notify(T("Msg.LiveStarted"), ToastKind::Ok);
			} else {
				notify(T("Msg.RoomOpened"), ToastKind::Ok);
			}
		}
	}
	obs_log(LOG_INFO, "room is live (room=%lld)", static_cast<long long>(m_state.roomId));
	persistSettings();
	emitChanged();
}

void LiveController::endLive()
{
	if (m_shuttingDown || m_state.auth != Auth::SignedIn || m_state.busy != Busy::None)
		return;
	m_ending = true;
	obs_log(LOG_INFO, "ending the broadcast");
	// Only stop OBS when it is actually pushing to Bilibili: a stream to another
	// destination must survive closing the Bilibili room.
	if ((m_state.obsStreaming || m_state.streamStarting) && m_state.streamTargetIsBilibili)
		obs::stopStreaming();
	closeRoom();
}

void LiveController::closeRoom()
{
	if (m_shuttingDown || m_state.roomId <= 0) {
		m_ending = false;
		return;
	}
	if (m_state.busy != Busy::None) {
		// Keep the intent: a sync/start is in flight and will finish soon, then the room
		// is closed. Dropping it would leave the room live with no way to close it.
		if (!m_closeRoomPending) {
			m_closeRoomPending = true;
			obs_log(LOG_INFO, "room close requested while a Kaibo call is in flight; closing afterwards");
		}
		return;
	}
	m_state.busy = Busy::Ending;
	emitChanged();

	struct Result {
		ApiError error;
		CookieJar cookies;
	};
	auto result = std::make_shared<Result>();
	auto client = m_client;
	const int64_t roomId = m_state.roomId;
	const int generation = m_sessionGeneration;
	worker::run(
		this,
		[client, roomId, result] {
			result->error = client->stopLive(roomId);
			result->cookies = client->cookies();
		},
		[this, result, generation] {
			if (m_shuttingDown)
				return;
			if (generation != m_sessionGeneration) {
				if (m_state.auth == Auth::SignedIn)
					finishBusyOperation();
				return;
			}
			m_closeRoomPending = false;
			finishBusyOperation();
			if (handleSessionExpiry(result->error)) {
				emitChanged();
				return;
			}
			if (result->error) {
				logApiFailure("ending the broadcast", "Room/stopLive", result->error);
				m_ending = false;
				notify(T("Error.StopLive"), ToastKind::Error, describe(result->error));
				emitChanged();
				return;
			}
			absorbClientCookies(result->cookies);
			obs_log(LOG_INFO, "closed the live room (room=%lld)", static_cast<long long>(roomId));
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

	if (restoreAfter) {
		if (m_state.obsStreaming || m_state.streamStarting) {
			m_pendingRestore = true; // after OBS has finished stopping
		} else {
			restorePreviousService();
		}
	} else {
		// The option is off: a captured service must not be restored in a later session.
		m_settings.serviceBackup = {};
		m_store.clearServiceBackup();
	}
	persistSettings();
	obs_log(LOG_INFO, "broadcast ended (duration %lld s)", static_cast<long long>(duration));
	notify(duration > 0 ? T("Msg.LiveEnded").arg(durationText(duration)) : T("Msg.RoomClosed"), ToastKind::Info);
	emitChanged();
}

void LiveController::restorePreviousService()
{
	if (!m_settings.serviceBackup.valid())
		return;
	if (!obs::streamTargetIsBilibili()) {
		// OBS is configured for something else now (the person changed it while live):
		// restoring would silently undo that, so the newer configuration wins and the
		// stale backup is dropped.
		obs_log(LOG_INFO, "OBS stream settings were changed outside Kaibo; not restoring the previous ones");
		m_settings.serviceBackup = {};
		m_store.clearServiceBackup();
		persistSettings();
		return;
	}
	std::string error;
	if (obs::restoreStreamService(m_settings.serviceBackup, error)) {
		m_settings.serviceBackup = {};
		m_store.clearServiceBackup();
		persistSettings();
		obs_log(LOG_INFO, "restored the previous OBS stream settings");
	} else {
		obs_log(LOG_WARNING, "could not restore the previous stream settings: %s", error.c_str());
	}
}

void LiveController::startObsStream()
{
	if (m_shuttingDown)
		return;
	m_state.dropped = false;
	// Only start OBS when its target really is Bilibili. Otherwise (the person changed
	// the OBS service, or Kaibo's credentials are gone after a restart) pushing would go
	// to the wrong destination, so re-run the go-live flow to rewrite the service and get
	// fresh credentials first.
	if (m_state.room.live &&
	    (!m_state.streamTargetIsBilibili || m_state.server.isEmpty() || m_state.key.isEmpty())) {
		obs_log(LOG_INFO, "start-stream requested without usable Bilibili credentials; re-running go-live");
		emitChanged();
		goLive();
		return;
	}
	obs::startStreaming();
	emitChanged();
}

void LiveController::stopObsStream()
{
	if (m_shuttingDown)
		return;
	obs::stopStreaming();
}

void LiveController::setOptions(const Options &options)
{
	m_state.options = options;
	// The captured service belongs to the live session, not to the checkbox: while a
	// room is live, toggling the option off and on again must not throw it away.
	if (!options.restoreService && !m_state.room.live) {
		m_settings.serviceBackup = {};
		m_store.clearServiceBackup();
	}
	persistSettings();
	emitChanged();
}

} // namespace kaibo
