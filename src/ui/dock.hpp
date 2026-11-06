// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QTimer>
#include <QWidget>

class QAction;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QPushButton;
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
	QWidget *buildFaceAuthCard();
	QWidget *buildActionBar();

	void scheduleRender();
	void render();
	void renderQr();
	void renderBanner();
	void renderFaceAuth();
	void renderGoLive();
	void renderClock();
	void updateElidedTexts();
	// OBS 33: tell the person how to make this panel fill the whole right column.
	void updateLayoutHint();
	void dismissLayoutHint();
	void loadLayoutHintDismissed();
	QAction *sideDocksAction();
	void applyTheme();
	void openAreaPicker();
	void showTitleHistory();
	void showAccountMenu();
	void execMenuAt(QMenu &menu, QWidget *anchor);
	void confirmDeleteData();
	void copyText(const QString &text, const QString &what);
	void readOptions();
	void setSettingsOpen(bool open);

	LiveController *m_controller;
	bool m_settingsOpen = false;
	bool m_rendering = false;
	bool m_newsExpanded = false;

	// Coalesces bursts of controller changed() (every title keystroke) into one pass.
	QTimer m_renderTimer;
	// Re-elides the text labels once a resize settled.
	QTimer m_elideTimer;
	QTimer m_clockTimer;
	qint64 m_lastRefreshMs = 0;

	// Last-applied banner/button states, so render() does not re-wire or re-rasterise
	// anything that did not change.
	QString m_bannerKey;
	QString m_loginBannerKey;
	QString m_goLiveIconKey;

	// Full (unelided) text of the labels that can be wider than the dock.
	QString m_nameFull;
	QString m_roomLabelFull;
	QString m_dataPathFull;
	QString m_areaFull;
	bool m_hintDismissed = false;
	bool m_hintDismissLoaded = false;
	QAction *m_sideDocksAction = nullptr;
	int m_hintRetries = 0; // OBS may build its dock menu after the panel appears

	// header
	QLabel *m_logo = nullptr;
	StatusPill *m_pill = nullptr;
	QPushButton *m_settingsButton = nullptr;
	QPushButton *m_backButton = nullptr;

	QWidget *m_hintWrap = nullptr;
	Banner *m_hintBanner = nullptr;
	QScrollArea *m_scroll = nullptr;
	QWidget *m_loadingPage = nullptr;
	QWidget *m_loginPage = nullptr;
	QWidget *m_mainPage = nullptr;
	QWidget *m_settingsPage = nullptr;

	// login
	Banner *m_loginBanner = nullptr;
	Banner *m_legacyBanner = nullptr;
	QrView *m_qr = nullptr;
	QLabel *m_qrTitle = nullptr;
	QLabel *m_qrHint = nullptr;

	// main
	Banner *m_banner = nullptr;
	Avatar *m_avatar = nullptr;
	QLabel *m_name = nullptr;
	QLabel *m_roomLabel = nullptr;
	QPushButton *m_moreButton = nullptr;
	QWidget *m_stats = nullptr;
	QLabel *m_online = nullptr;
	QLabel *m_followers = nullptr;
	QLineEdit *m_title = nullptr;
	QLabel *m_titleCounter = nullptr;
	QLabel *m_titleError = nullptr;
	QPushButton *m_titleHistoryButton = nullptr;
	QPushButton *m_areaButton = nullptr;
	QLabel *m_areaText = nullptr;
	QLabel *m_areaHint = nullptr;
	QPushButton *m_newsToggle = nullptr;
	QPlainTextEdit *m_news = nullptr;
	QLabel *m_newsCounter = nullptr;
	QLabel *m_newsError = nullptr;
	QLabel *m_newsHint = nullptr;
	QLabel *m_syncHint = nullptr;
	QPushButton *m_sync = nullptr;
	QWidget *m_actionBar = nullptr;
	QWidget *m_clockRow = nullptr;
	QLabel *m_clock = nullptr;
	QPushButton *m_goLive = nullptr;
	QLabel *m_goLiveHint = nullptr;

	// face verification (Bilibili 60024 / 60043)
	QWidget *m_faceCard = nullptr;
	QLabel *m_faceHeading = nullptr;
	QLabel *m_faceHint = nullptr;
	QrView *m_faceQr = nullptr;
	QPushButton *m_faceBrowser = nullptr;
	QPushButton *m_faceCancel = nullptr;
	QPushButton *m_faceConfirm = nullptr;

	QWidget *m_streamCard = nullptr;
	QPushButton *m_streamToggle = nullptr;
	QLabel *m_streamChevron = nullptr;
	QWidget *m_streamBody = nullptr;
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

	// Transient rows live in the dock's vertical layout, under the scrolling content,
	// so a toast never paints over a control or eats a click meant for one.
	QWidget *m_toastWrap = nullptr;
	AreaPicker *m_picker = nullptr;
	Toast *m_toast = nullptr;
};

} // namespace kaibo::ui
