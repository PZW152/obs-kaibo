// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "app/obs_bridge.hpp"
#include "app/store.hpp"
#include "core/bili_client.hpp"
#include "core/live_state.hpp"
#include "core/preflight.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>
#include <vector>

namespace kaibo {

class CurlTransport;

// The single source of truth for Kaibo. Lives on the UI thread; every network
// call runs on the worker thread through a shared BiliClient. Widgets read
// state() and listen to changed(); they never talk to the network themselves.
class LiveController : public QObject {
	Q_OBJECT

public:
	enum class Auth { Unknown, SignedOut, Checking, SignedIn };
	enum class Busy { None, Syncing, GoingLive, Ending };
	using Phase = LivePhase;
	enum class QrPhase { Idle, Loading, Waiting, Scanned, Expired, Failed };
	// Bilibili's start-live check can ask for a face verification first. The person
	// scans a code on the phone; the live API only sees that a moment later, so the
	// controller retries on its own instead of making them scan again.
	enum class FaceAuth { None, Required, Retrying };
	enum class ToastKind { Ok, Info, Error };

	// One local go-live check with its message already localized. `blocking` is true for
	// errors, which goLive() refuses to pass; warnings are advisory and never block.
	struct PreflightItem {
		bool blocking = false;
		QString message;
	};

	struct State {
		Auth auth = Auth::Unknown;
		Busy busy = Busy::None;
		bool offline = false;         // last refresh failed for network reasons
		bool legacyAvailable = false; // old plugin left a sign-in we can import

		Profile profile;
		QImage avatar;
		int64_t roomId = 0;
		bool roomLoaded = false;
		RoomInfo room;
		std::string news;
		bool newsLoaded = false;

		QString titleDraft;
		int areaDraft = 0;
		QString newsDraft;

		std::vector<AreaGroup> areas;
		bool areasLoading = false;

		bool obsStreaming = false;
		bool streamStarting = false; // OBS is connecting the output (STREAMING_STARTING)
		bool streamTargetIsBilibili = false;
		bool reconnecting = false;
		bool dropped = false;
		QString server;
		QString key;
		int64_t liveSince = 0; // Unix seconds

		QrPhase qr = QrPhase::Idle;
		QString qrUrl;
		QString qrError;

		FaceAuth face = FaceAuth::None;
		// The link Bilibili returned for this verification, empty when it gave none.
		// Kaibo never invents one: a verification page opened without Bilibili's own
		// token cannot start a check, and the failed attempt still counts against the
		// account's daily verification quota.
		QString faceAuthUrl;
		QString faceAuthMessage; // Bilibili's own wording for this verification, if any
		int faceAuthAttempt = 0; // retries already scheduled, 0 while waiting for a scan

		Options options;
		std::vector<int> recentAreas;
		std::vector<std::string> recentTitles;

		// Recomputed on every changed() from the cached state; the dock can render it
		// without calling preflight() itself.
		std::vector<PreflightItem> preflight;
	};

	explicit LiveController(QObject *parent = nullptr);
	~LiveController() override;

	const State &state() const { return m_state; }
	Phase phase() const;
	bool hasUnsyncedChanges() const;
	bool titleTooLong() const;
	bool newsTooLong() const;
	QString describe(const ApiError &error) const;
	QString storageDirectory() const
	{
		return QDir::toNativeSeparators(QString::fromStdString(m_store.directory()));
	}

	// Go-live checks that need no network: title/announcement length, area choice and
	// lock state, session, room state, and whether the last request failed. Errors in the
	// result block goLive(); warnings are advisory. state().preflight carries the same
	// list, refreshed on every changed().
	std::vector<PreflightItem> preflight() const;
	bool preflightBlocked() const;

	// Lifecycle (called from plugin-main on frontend events).
	void startup();
	void onStreamingStarting();
	void onStreamingStarted();
	void onStreamingStopped();
	void shutdownForExit();

	// Sign-in
	void startQrLogin();
	void stopQrLogin();
	void importLegacy();
	void dismissLegacy();
	void signOut();
	bool deleteAllData(QString &error);

	// Room
	void refresh();
	void ensureAreas();
	void setTitleDraft(const QString &title);
	void setAreaDraft(int areaId);
	void setNewsDraft(const QString &news);
	void syncRoom();

	// Going live and ending
	void goLive();
	// Face verification for the current go-live is believed to be done: retry the
	// start with backoff until Bilibili accepts it.
	void confirmFaceAuth();
	// Give up on the pending face verification (the person closed the prompt).
	void cancelFaceAuth();
	void endLive();
	void closeRoom();
	void startObsStream();
	void stopObsStream();

	void setOptions(const Options &options);
	// The poll slows down while nobody can see the dock.
	void setDockVisible(bool visible);

	static constexpr int kTitleLimit = kMaxTitleLength;
	static constexpr int kNewsLimit = kMaxNewsLength;

signals:
	void changed();
	void qrChanged();
	void avatarChanged();
	void toast(const QString &text, int kind, const QString &detail);

private:
	void emitChanged();
	void updatePreflight();
	// Matches the poll rate to the current state; call after anything it depends on changes.
	void reschedulePoll();
	void noteRequestFailure(const ApiError &error);
	// True when a worker reply must be dropped: OBS is exiting or the session it belongs
	// to is gone. Releases the busy flag the call held.
	bool staleReply(int generation);
	RoomEdits pendingEdits() const;
	// Records edits Bilibili accepted; the caller persists settings when it needs to.
	void commitRoomEdits(const RoomEdits &edits);
	// Signs the person out when `error` says Bilibili no longer accepts the session.
	// Returns true when the error was handled (callers must then stop their flow).
	bool handleSessionExpiry(const ApiError &error);
	// Puts the OBS service Kaibo replaced back, but only while OBS still holds the
	// Bilibili service Kaibo wrote; a newer configuration from the person wins.
	void restorePreviousService();
	// Keeps the input-mode session in sync when a request rotated cookies, and persists
	// it when it actually changed (never rewrites the sealed file otherwise).
	void absorbClientCookies(const CookieJar &jar);
	// busy → None, then replays a close-room request that arrived while busy.
	void finishBusyOperation();
	void validateSession(const CookieJar &jar, bool fromLogin);
	void applyRoom(const RoomInfo &room);
	void loadAvatar();
	void pollQr();
	void handleStartLiveResult(const ApiError &error, const LiveStart &start, bool startOutput);
	void retryFaceAuth();
	void requestFaceAuthRetry();
	void clearFaceAuth();
	void finishLiveStart(const LiveStart &start, bool startOutput);
	// Closes the room once the output that was asked to stop really stopped (or the
	// bounded wait expired); between STARTING and STARTED the two must not race.
	void closeRoomAfterStreamStop();
	void finishRoomClosed(bool restoreAfter);
	void onOutputEvent(obs::OutputEvent event);
	void resetSession();
	void persistSettings();
	void notify(const QString &text, ToastKind kind, const QString &detail = {});
	Settings settingsSnapshot() const;

	State m_state;
	Store m_store;
	Settings m_settings;
	CookieJar m_cookies; // UI-side mirror of the worker client's session

	std::shared_ptr<CurlTransport> m_transport;
	std::shared_ptr<BiliClient> m_client; // touched only on the worker thread

	std::unique_ptr<obs::OutputWatcher> m_watcher;
	QTimer m_pollTimer;
	QTimer m_qrTimer;
	QTimer m_faceAuthTimer;
	QTimer m_changedTimer;     // coalesces bursts of state changes into one UI repaint
	QTimer m_stopTimeoutTimer; // bounded wait for the output to stop before closing the room
	QElapsedTimer m_qrAge;
	QElapsedTimer m_sessionChecked; // since an authenticated call last ran
	std::string m_qrKey;
	int m_qrRequestId = 0; // drops a QR reply that a newer request/stop replaced
	bool m_qrInFlight = false;
	bool m_refreshInFlight = false;
	bool m_ending = false;
	bool m_pendingRestore = false;
	bool m_closeAfterStreamStop = false;
	bool m_startAfterGoLive = false; // "start streaming" asked for this go-live
	bool m_shuttingDown = false;
	bool m_closeRoomPending = false; // close was asked for while another call was busy
	int m_faceAuthAttempt = 0;
	bool m_faceStartOutput = false; // the go-live behind the verification also starts OBS
	bool m_dockVisible = false;
	int m_offlineStreak = 0;     // consecutive failed requests while offline
	int m_sessionGeneration = 0; // drops stale replies after sign-out
};

} // namespace kaibo
