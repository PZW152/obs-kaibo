// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QTimer>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QScrollArea;

namespace kaibo {
class LiveController;
}

namespace kaibo::ui {

class AreaPicker;
class Avatar;
class Banner;
class QrView;
class StatusPill;
class Toast;
class ToggleSwitch;

// The Kaibo panel docked in OBS. Pure view: it renders LiveController::state()
// and forwards intents back to the controller.
class KaiboDock : public QWidget {
	Q_OBJECT
public:
	KaiboDock(LiveController *controller, QWidget *parent = nullptr);

	// Hotkey entry points (UI thread).
	void requestGoLive();
	void requestEndLive();

protected:
	void changeEvent(QEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;

private:
	QWidget *buildHeader();
	QWidget *buildLoadingPage();
	QWidget *buildLoginPage();
	QWidget *buildMainPage();
	QWidget *buildSettingsPage();

	void render();
	void renderQr();
	void renderBanner();
	void renderGoLive();
	void renderClock();
	void applyTheme();
	void openAreaPicker();
	void showTitleHistory();
	void showAccountMenu();
	void showFaceAuth(const QString &url);
	void confirmDeleteData();
	void copyText(const QString &text);
	void readOptions();
	void setSettingsOpen(bool open);

	LiveController *m_controller;
	bool m_settingsOpen = false;
	bool m_rendering = false;

	// header
	QLabel *m_logo = nullptr;
	StatusPill *m_pill = nullptr;
	QPushButton *m_settingsButton = nullptr;
	QPushButton *m_backButton = nullptr;

	QScrollArea *m_scroll = nullptr;
	QStackedWidget *m_pages = nullptr;
	QWidget *m_loadingPage = nullptr;
	QWidget *m_loginPage = nullptr;
	QWidget *m_mainPage = nullptr;
	QWidget *m_settingsPage = nullptr;

	// login
	Banner *m_legacyBanner = nullptr;
	QrView *m_qr = nullptr;
	QLabel *m_qrTitle = nullptr;
	QLabel *m_qrHint = nullptr;

	// main
	Banner *m_banner = nullptr;
	Avatar *m_avatar = nullptr;
	QLabel *m_name = nullptr;
	QLabel *m_roomLabel = nullptr;
	QWidget *m_stats = nullptr;
	QLabel *m_online = nullptr;
	QLabel *m_followers = nullptr;
	QLineEdit *m_title = nullptr;
	QLabel *m_titleCounter = nullptr;
	QLabel *m_titleError = nullptr;
	QPushButton *m_areaButton = nullptr;
	QLabel *m_areaText = nullptr;
	QPushButton *m_newsToggle = nullptr;
	QPlainTextEdit *m_news = nullptr;
	QLabel *m_newsCounter = nullptr;
	QLabel *m_syncHint = nullptr;
	QPushButton *m_sync = nullptr;
	QWidget *m_clockRow = nullptr;
	QLabel *m_clock = nullptr;
	QPushButton *m_goLive = nullptr;
	QLabel *m_goLiveHint = nullptr;
	QWidget *m_streamCard = nullptr;
	QLineEdit *m_server = nullptr;
	QLineEdit *m_key = nullptr;
	QPushButton *m_keyReveal = nullptr;

	// settings
	ToggleSwitch *m_optAutoStart = nullptr;
	ToggleSwitch *m_optCloseOnStop = nullptr;
	ToggleSwitch *m_optCloseOnExit = nullptr;
	ToggleSwitch *m_optRestore = nullptr;
	ToggleSwitch *m_optConfirm = nullptr;
	QLabel *m_accountLabel = nullptr;
	QPushButton *m_signOut = nullptr;
	QLabel *m_dataPath = nullptr;

	AreaPicker *m_picker = nullptr;
	Toast *m_toast = nullptr;
	QTimer m_clockTimer;
};

} // namespace kaibo::ui
