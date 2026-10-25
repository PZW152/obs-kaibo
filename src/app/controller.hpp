// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "app/obs_bridge.hpp"
#include "app/store.hpp"
#include "core/bili_client.hpp"

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
	// Room state × OBS output state, as the person should understand it.
	enum class Phase { Offline, Live, Reconnecting, RoomOnly, Dropped, StreamOnly };
	enum class QrPhase { Idle, Loading, Waiting, Scanned, Expired, Failed };
	// Bilibili's start-live check can ask for a face verification first. The person
	// scans a code on the phone; the live API only sees that a moment later, so the
	// controller retries on its own instead of making them scan again.
	enum class FaceAuth { None, Required, Retrying };
	enum class ToastKind { Ok, Info, Error };

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
		QString faceAuthUrl;     // the code to show while the person verifies
		int faceAuthAttempt = 0; // retries already scheduled, 0 while waiting for a scan

		Options options;
		std::vector<int> recentAreas;
		std::vector<std::string> recentTitles;
	};

	explicit LiveController(QObject *parent = nullptr);
	~LiveController() override;

	const State &state() const { return m_state; }
	Phase phase() const;
	bool hasUnsyncedChanges() const;
	bool titleTooLong() const;
	QString describe(const ApiError &error) const;
	QString storageDirectory() const
	{
		return QDir::toNativeSeparators(QString::fromStdString(m_store.directory()));
	}

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

	static constexpr int kTitleLimit = 40;
	static constexpr int kNewsLimit = 60;

signals:
	void changed();
	void qrChanged();
	void avatarChanged();
	void toast(const QString &text, int kind, const QString &detail);

private:
	void validateSession(const CookieJar &jar, bool fromLogin);
	void applyRoom(const RoomInfo &room);
	void loadAvatar();
	void pollQr();
	void handleStartLiveResult(const ApiError &error, const LiveStart &start);
	void retryFaceAuth();
	void requestFaceAuthRetry();
	void clearFaceAuth();
	void finishLiveStart(const LiveStart &start);
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
	QElapsedTimer m_qrAge;
	std::string m_qrKey;
	bool m_qrInFlight = false;
	bool m_refreshInFlight = false;
	bool m_ending = false;
	bool m_pendingRestore = false;
	bool m_startedStreamForLive = false;
	int m_faceAuthAttempt = 0;
	int m_sessionGeneration = 0; // drops stale replies after sign-out
};

} // namespace kaibo
