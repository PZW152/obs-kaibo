// SPDX-License-Identifier: GPL-2.0-only
#include "ui/dock.hpp"
#include "app/controller.hpp"
#include "app/i18n.hpp"
#include "app/secret_store.hpp"
#include "ui/area_picker.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <plugin-support.h>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace kaibo::ui {
namespace {

using Phase = LiveController::Phase;
using Busy = LiveController::Busy;
using Auth = LiveController::Auth;
using QrPhase = LiveController::QrPhase;

const char *const kLiveCenterUrl = "https://link.bilibili.com/p/center/index";
const char *const kProjectUrl = "https://github.com/PZW152/obs-kaibo";

Theme &theme()
{
	return Theme::instance();
}

QWidget *iconText(const QString &icon, QLabel **textOut, QWidget *parent)
{
	auto *w = new QWidget(parent);
	auto *row = new QHBoxLayout(w);
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(4);
	auto *i = new QLabel(w);
	i->setProperty("kbIcon", icon);
	i->setProperty("kbIconToken", QStringLiteral("ink-muted"));
	i->setFixedSize(13, 13);
	i->setScaledContents(true);
	row->addWidget(i);
	*textOut = label({}, "caption", w);
	(*textOut)->setWordWrap(false);
	row->addWidget(*textOut);
	return w;
}

QWidget *settingRow(const QString &title, const QString &hint, ToggleSwitch **out, QWidget *parent)
{
	auto *w = new QWidget(parent);
	auto *row = new QHBoxLayout(w);
	row->setContentsMargins(0, 8, 0, 8);
	row->setSpacing(12);
	auto *col = new QVBoxLayout();
	col->setSpacing(2);
	auto *t = label(title, nullptr, w);
	t->setWordWrap(true);
	col->addWidget(t);
	if (!hint.isEmpty())
		col->addWidget(label(hint, "caption", w));
	row->addLayout(col, 1);
	*out = new ToggleSwitch(w);
	(*out)->setAccessibleName(title);
	row->addWidget(*out, 0, Qt::AlignTop);
	return w;
}

QString roomLink(int64_t roomId)
{
	return QStringLiteral("https://live.bilibili.com/%1").arg(roomId);
}

} // namespace

KaiboDock::KaiboDock(LiveController *controller, QWidget *parent) : QWidget(parent), m_controller(controller)
{
	setObjectName(QStringLiteral("KaiboDock"));
	setAttribute(Qt::WA_StyledBackground);
	setMinimumWidth(theme().length(QStringLiteral("dock-min"), 280));
	theme().load();
	theme().followPalette(QApplication::palette());

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(0);
	root->addWidget(buildHeader());

	m_scroll = new QScrollArea(this);
	m_scroll->setObjectName(QStringLiteral("KbScroll"));
	m_scroll->setWidgetResizable(true);
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	auto *content = new QWidget(m_scroll);
	content->setObjectName(QStringLiteral("KbContent"));
	auto *col = new QVBoxLayout(content);
	col->setContentsMargins(12, 4, 12, 12);
	col->setSpacing(0);
	m_pages = new QStackedWidget(content);
	m_loadingPage = buildLoadingPage();
	m_loginPage = buildLoginPage();
	m_mainPage = buildMainPage();
	m_settingsPage = buildSettingsPage();
	for (QWidget *page : {m_loadingPage, m_loginPage, m_mainPage, m_settingsPage})
		m_pages->addWidget(page);
	col->addWidget(m_pages);
	col->addStretch(1);
	m_scroll->setWidget(content);
	root->addWidget(m_scroll, 1);

	m_picker = new AreaPicker(this);
	connect(m_picker, &AreaPicker::picked, m_controller, &LiveController::setAreaDraft);
	m_toast = new Toast(this);

	m_clockTimer.setInterval(1000);
	connect(&m_clockTimer, &QTimer::timeout, this, &KaiboDock::renderClock);

	connect(m_controller, &LiveController::changed, this, &KaiboDock::render);
	connect(m_controller, &LiveController::qrChanged, this, &KaiboDock::renderQr);
	connect(m_controller, &LiveController::avatarChanged, this,
		[this] { m_avatar->setImage(m_controller->state().avatar); });
	connect(m_controller, &LiveController::toast, this,
		[this](const QString &text, int kind, const QString &detail) {
			m_toast->showMessage(kind, text, detail);
		});
	connect(m_controller, &LiveController::faceAuthRequired, this, &KaiboDock::showFaceAuth);

	applyTheme();
	render();
}

// ------------------------------------------------------------------ building

QWidget *KaiboDock::buildHeader()
{
	auto *bar = new QWidget(this);
	bar->setObjectName(QStringLiteral("KbHeader"));
	auto *row = new QHBoxLayout(bar);
	row->setContentsMargins(12, 10, 12, 8);
	row->setSpacing(8);

	m_backButton = iconButton(QStringLiteral("chevron-left"), T("Common.Back"), bar);
	connect(m_backButton, &QPushButton::clicked, this, [this] { setSettingsOpen(false); });
	row->addWidget(m_backButton);

	m_logo = new QLabel(bar);
	m_logo->setFixedSize(18, 18);
	m_logo->setScaledContents(true);
	row->addWidget(m_logo);
	row->addWidget(label(QStringLiteral("Kaibo"), "brand", bar));
	row->addStretch(1);
	m_pill = new StatusPill(bar);
	row->addWidget(m_pill);
	m_settingsButton = iconButton(QStringLiteral("settings"), T("Settings.Title"), bar);
	connect(m_settingsButton, &QPushButton::clicked, this, [this] { setSettingsOpen(true); });
	row->addWidget(m_settingsButton);
	return bar;
}

QWidget *KaiboDock::buildLoadingPage()
{
	auto *page = new QWidget(this);
	auto *col = new QVBoxLayout(page);
	col->setContentsMargins(0, 0, 0, 0);
	auto *c = card(page);
	auto *cc = new QVBoxLayout(c);
	cc->setContentsMargins(24, 32, 24, 32);
	auto *text = label(T("Loading.Text"), "caption", c);
	text->setAlignment(Qt::AlignCenter);
	cc->addWidget(text);
	col->addWidget(c);
	return page;
}

QWidget *KaiboDock::buildLoginPage()
{
	auto *page = new QWidget(this);
	auto *col = new QVBoxLayout(page);
	col->setContentsMargins(0, 0, 0, 0);
	col->setSpacing(12);

	m_legacyBanner = new Banner(page);
	connect(m_legacyBanner, &Banner::primaryClicked, m_controller, &LiveController::importLegacy);
	connect(m_legacyBanner, &Banner::secondaryClicked, m_controller, &LiveController::dismissLegacy);
	col->addWidget(m_legacyBanner);

	auto *c = card(page);
	auto *cc = new QVBoxLayout(c);
	cc->setContentsMargins(16, 24, 16, 20);
	cc->setSpacing(12);
	cc->setAlignment(Qt::AlignHCenter);
	m_qrTitle = label(T("Login.Title"), "title", c);
	m_qrTitle->setAlignment(Qt::AlignCenter);
	cc->addWidget(m_qrTitle, 0, Qt::AlignHCenter);
	m_qr = new QrView(c);
	connect(m_qr, &QrView::clicked, m_controller, &LiveController::startQrLogin);
	cc->addWidget(m_qr, 0, Qt::AlignHCenter);
	m_qrHint = label(T("Login.Hint"), "caption", c);
	m_qrHint->setAlignment(Qt::AlignCenter);
	cc->addWidget(m_qrHint);

	QLabel *privacyText = nullptr;
	auto *privacy = iconText(QStringLiteral("shield"), &privacyText, c);
	privacyText->setText(T("Login.Privacy"));
	cc->addWidget(privacy, 0, Qt::AlignHCenter);
	col->addWidget(c);
	return page;
}

QWidget *KaiboDock::buildMainPage()
{
	auto *page = new QWidget(this);
	auto *col = new QVBoxLayout(page);
	col->setContentsMargins(0, 0, 0, 0);
	col->setSpacing(12);

	m_banner = new Banner(page);
	col->addWidget(m_banner);

	// Account
	auto *account = card(page);
	auto *ac = new QVBoxLayout(account);
	ac->setContentsMargins(12, 12, 12, 12);
	ac->setSpacing(10);
	auto *who = new QHBoxLayout();
	who->setSpacing(12);
	m_avatar = new Avatar(account);
	who->addWidget(m_avatar);
	auto *names = new QVBoxLayout();
	names->setSpacing(0);
	m_name = label({}, "title", account);
	m_name->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_roomLabel = label({}, "caption", account);
	m_roomLabel->setWordWrap(false);
	names->addWidget(m_name);
	names->addWidget(m_roomLabel);
	who->addLayout(names, 1);
	auto *openRoom = iconButton(QStringLiteral("external"), T("Header.OpenRoom"), account);
	connect(openRoom, &QPushButton::clicked, this, [this] {
		const auto &s = m_controller->state();
		QDesktopServices::openUrl(
			QUrl(s.roomId > 0 ? roomLink(s.roomId) : QString::fromLatin1(kLiveCenterUrl)));
	});
	who->addWidget(openRoom, 0, Qt::AlignTop);
	auto *more = iconButton(QStringLiteral("more"), T("Common.More"), account);
	connect(more, &QPushButton::clicked, this, &KaiboDock::showAccountMenu);
	who->addWidget(more, 0, Qt::AlignTop);
	ac->addLayout(who);
	m_stats = new QWidget(account);
	auto *stats = new QHBoxLayout(m_stats);
	stats->setContentsMargins(0, 0, 0, 0);
	stats->setSpacing(12);
	stats->addWidget(iconText(QStringLiteral("users"), &m_online, m_stats));
	stats->addWidget(iconText(QStringLiteral("heart"), &m_followers, m_stats));
	stats->addStretch(1);
	ac->addWidget(m_stats);
	col->addWidget(account);

	// Room
	auto *room = card(page);
	auto *rc = new QVBoxLayout(room);
	rc->setContentsMargins(12, 12, 12, 12);
	rc->setSpacing(6);
	auto *titleHead = new QHBoxLayout();
	titleHead->addWidget(label(T("Room.Title"), "strong", room));
	titleHead->addStretch(1);
	m_titleCounter = label({}, "faint", room);
	m_titleCounter->setWordWrap(false);
	titleHead->addWidget(m_titleCounter);
	rc->addLayout(titleHead);
	auto *titleRow = new QHBoxLayout();
	titleRow->setSpacing(6);
	m_title = new QLineEdit(room);
	m_title->setPlaceholderText(T("Room.TitlePlaceholder"));
	m_title->setAccessibleName(T("Room.Title"));
	connect(m_title, &QLineEdit::textEdited, m_controller, &LiveController::setTitleDraft);
	connect(m_title, &QLineEdit::returnPressed, m_controller, &LiveController::syncRoom);
	titleRow->addWidget(m_title, 1);
	auto *history = iconButton(QStringLiteral("history"), T("Room.TitleHistory"), room);
	connect(history, &QPushButton::clicked, this, &KaiboDock::showTitleHistory);
	titleRow->addWidget(history);
	rc->addLayout(titleRow);
	m_titleError = label({}, "danger", room);
	rc->addWidget(m_titleError);

	rc->addSpacing(6);
	rc->addWidget(label(T("Room.Area"), "strong", room));
	m_areaButton = button({}, "select", room);
	m_areaButton->setAccessibleName(T("Room.Area"));
	auto *areaInner = new QHBoxLayout(m_areaButton);
	areaInner->setContentsMargins(10, 0, 8, 0);
	m_areaText = new QLabel(m_areaButton);
	m_areaText->setAttribute(Qt::WA_TransparentForMouseEvents);
	areaInner->addWidget(m_areaText, 1);
	auto *chevron = new QLabel(m_areaButton);
	chevron->setProperty("kbIcon", QStringLiteral("chevron-down"));
	chevron->setProperty("kbIconToken", QStringLiteral("ink-muted"));
	chevron->setFixedSize(16, 16);
	chevron->setScaledContents(true);
	chevron->setAttribute(Qt::WA_TransparentForMouseEvents);
	areaInner->addWidget(chevron);
	connect(m_areaButton, &QPushButton::clicked, this, &KaiboDock::openAreaPicker);
	rc->addWidget(m_areaButton);

	rc->addSpacing(6);
	auto *newsHead = new QHBoxLayout();
	m_newsToggle = button(T("Room.NewsAdd"), "ghost", room);
	m_newsToggle->setProperty("kbSize", QStringLiteral("sm"));
	m_newsToggle->setProperty("kbIcon", QStringLiteral("megaphone"));
	m_newsToggle->setCheckable(true);
	newsHead->addWidget(m_newsToggle);
	newsHead->addStretch(1);
	m_newsCounter = label({}, "faint", room);
	m_newsCounter->setWordWrap(false);
	newsHead->addWidget(m_newsCounter);
	rc->addLayout(newsHead);
	m_news = new QPlainTextEdit(room);
	m_news->setPlaceholderText(T("Room.NewsPlaceholder"));
	m_news->setAccessibleName(T("Room.News"));
	m_news->setFixedHeight(64);
	m_news->setTabChangesFocus(true);
	connect(m_news, &QPlainTextEdit::textChanged, this, [this] {
		if (!m_rendering)
			m_controller->setNewsDraft(m_news->toPlainText());
	});
	connect(m_newsToggle, &QPushButton::toggled, this, [this](bool on) {
		m_news->setVisible(on);
		m_newsCounter->setVisible(on);
		if (on)
			m_news->setFocus();
	});
	rc->addWidget(m_news);

	rc->addSpacing(4);
	auto *syncRow = new QHBoxLayout();
	m_syncHint = label({}, "caption", room);
	syncRow->addWidget(m_syncHint, 1);
	m_sync = button(T("Room.Save"), nullptr, room);
	m_sync->setProperty("kbSize", QStringLiteral("sm"));
	connect(m_sync, &QPushButton::clicked, m_controller, &LiveController::syncRoom);
	syncRow->addWidget(m_sync, 0, Qt::AlignTop);
	rc->addLayout(syncRow);
	col->addWidget(room);

	// Go live
	auto *live = new QWidget(page);
	auto *lc = new QVBoxLayout(live);
	lc->setContentsMargins(0, 4, 0, 0);
	lc->setSpacing(8);
	m_clockRow = new QWidget(live);
	auto *clockRow = new QHBoxLayout(m_clockRow);
	clockRow->setContentsMargins(2, 0, 2, 0);
	clockRow->addWidget(label(T("Live.Duration"), "label", m_clockRow));
	clockRow->addStretch(1);
	m_clock = label(QStringLiteral("00:00:00"), "timer", m_clockRow);
	QFont clockFont = m_clock->font();
	clockFont.setStyleHint(QFont::Monospace);
	m_clock->setFont(clockFont);
	clockRow->addWidget(m_clock);
	lc->addWidget(m_clockRow);
	m_goLive = new QPushButton(live);
	m_goLive->setObjectName(QStringLiteral("KbGoLive"));
	m_goLive->setCursor(Qt::PointingHandCursor);
	m_goLive->setIconSize(QSize(20, 20));
	connect(m_goLive, &QPushButton::clicked, this, [this] {
		const auto phase = m_controller->phase();
		if (m_controller->state().busy != Busy::None)
			return;
		if (phase == Phase::Offline || phase == Phase::StreamOnly)
			m_controller->goLive();
		else
			requestEndLive();
	});
	lc->addWidget(m_goLive);
	m_goLiveHint = label({}, "caption", live);
	m_goLiveHint->setAlignment(Qt::AlignCenter);
	lc->addWidget(m_goLiveHint);
	col->addWidget(live);

	// Stream info (only after Kaibo opened the room in this session)
	m_streamCard = card(page);
	auto *sc = new QVBoxLayout(m_streamCard);
	sc->setContentsMargins(12, 12, 12, 12);
	sc->setSpacing(6);
	sc->addWidget(label(T("Info.Title"), "label", m_streamCard));
	sc->addWidget(label(T("Info.Server"), "strong", m_streamCard));
	auto *serverRow = new QHBoxLayout();
	m_server = new QLineEdit(m_streamCard);
	m_server->setReadOnly(true);
	m_server->setFont(theme().monoFont());
	m_server->setAccessibleName(T("Info.Server"));
	serverRow->addWidget(m_server, 1);
	auto *copyServer = iconButton(QStringLiteral("copy"), T("Common.Copy"), m_streamCard);
	connect(copyServer, &QPushButton::clicked, this, [this] { copyText(m_server->text()); });
	serverRow->addWidget(copyServer);
	sc->addLayout(serverRow);
	sc->addWidget(label(T("Info.Key"), "strong", m_streamCard));
	auto *keyRow = new QHBoxLayout();
	m_key = new QLineEdit(m_streamCard);
	m_key->setReadOnly(true);
	m_key->setEchoMode(QLineEdit::Password);
	m_key->setFont(theme().monoFont());
	m_key->setAccessibleName(T("Info.Key"));
	keyRow->addWidget(m_key, 1);
	m_keyReveal = iconButton(QStringLiteral("eye"), T("Info.Show"), m_streamCard);
	m_keyReveal->setCheckable(true);
	connect(m_keyReveal, &QPushButton::toggled, this, [this](bool on) {
		m_key->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
		m_keyReveal->setProperty("kbIcon", on ? QStringLiteral("eye-off") : QStringLiteral("eye"));
		m_keyReveal->setIcon(theme().icon(m_keyReveal->property("kbIcon").toString()));
		m_keyReveal->setToolTip(on ? T("Info.Hide") : T("Info.Show"));
	});
	keyRow->addWidget(m_keyReveal);
	auto *copyKey = iconButton(QStringLiteral("copy"), T("Common.Copy"), m_streamCard);
	connect(copyKey, &QPushButton::clicked, this, [this] { copyText(m_key->text()); });
	keyRow->addWidget(copyKey);
	sc->addLayout(keyRow);
	sc->addWidget(label(T("Info.Hint"), "caption", m_streamCard));
	col->addWidget(m_streamCard);

	return page;
}

QWidget *KaiboDock::buildSettingsPage()
{
	auto *page = new QWidget(this);
	auto *col = new QVBoxLayout(page);
	col->setContentsMargins(0, 0, 0, 0);
	col->setSpacing(12);

	auto *live = card(page);
	auto *lc = new QVBoxLayout(live);
	lc->setContentsMargins(12, 12, 12, 8);
	lc->setSpacing(0);
	lc->addWidget(label(T("Settings.GoLive"), "label", live));
	lc->addWidget(settingRow(T("Settings.AutoStart"), T("Settings.AutoStartHint"), &m_optAutoStart, live));
	lc->addWidget(divider(live));
	lc->addWidget(settingRow(T("Settings.CloseOnStop"), T("Settings.CloseOnStopHint"), &m_optCloseOnStop, live));
	lc->addWidget(divider(live));
	lc->addWidget(settingRow(T("Settings.CloseOnExit"), {}, &m_optCloseOnExit, live));
	lc->addWidget(divider(live));
	lc->addWidget(settingRow(T("Settings.Restore"), T("Settings.RestoreHint"), &m_optRestore, live));
	lc->addWidget(divider(live));
	lc->addWidget(settingRow(T("Settings.ConfirmEnd"), {}, &m_optConfirm, live));
	for (ToggleSwitch *t : {m_optAutoStart, m_optCloseOnStop, m_optCloseOnExit, m_optRestore, m_optConfirm})
		connect(t, &QPushButton::toggled, this, &KaiboDock::readOptions);
	col->addWidget(live);
	auto *hotkeys = label(T("Settings.Hotkeys"), "caption", page);
	col->addWidget(hotkeys);

	auto *account = card(page);
	auto *acc = new QVBoxLayout(account);
	acc->setContentsMargins(12, 12, 12, 12);
	acc->setSpacing(8);
	acc->addWidget(label(T("Settings.Account"), "label", account));
	m_accountLabel = label({}, nullptr, account);
	m_accountLabel->setWordWrap(true);
	acc->addWidget(m_accountLabel);
	m_signOut = button(T("Menu.Logout"), "danger", account);
	m_signOut->setProperty("kbIcon", QStringLiteral("logout"));
	m_signOut->setProperty("kbIconToken", QStringLiteral("danger"));
	connect(m_signOut, &QPushButton::clicked, this, [this] {
		const bool live = m_controller->state().room.live;
		const auto answer = QMessageBox::question(this, T("Menu.Logout"),
							  live ? T("Confirm.LogoutLive") : T("Confirm.LogoutText"));
		if (answer == QMessageBox::Yes)
			m_controller->signOut();
	});
	acc->addWidget(m_signOut, 0, Qt::AlignLeft);
	col->addWidget(account);

	auto *data = card(page);
	auto *dc = new QVBoxLayout(data);
	dc->setContentsMargins(12, 12, 12, 12);
	dc->setSpacing(8);
	dc->addWidget(label(T("Settings.Data"), "label", data));
	dc->addWidget(
		label(T("Settings.DataHint").arg(QString::fromLatin1(secret::protectionName())), "caption", data));
	m_dataPath = label(m_controller->storageDirectory(), "faint", data);
	m_dataPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
	dc->addWidget(m_dataPath);
	auto *dataRow = new QHBoxLayout();
	auto *openFolder = button(T("Settings.OpenFolder"), "ghost", data);
	openFolder->setProperty("kbSize", QStringLiteral("sm"));
	connect(openFolder, &QPushButton::clicked, this,
		[this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_controller->storageDirectory())); });
	dataRow->addWidget(openFolder);
	auto *wipe = button(T("Settings.DeleteData"), "danger", data);
	wipe->setProperty("kbSize", QStringLiteral("sm"));
	wipe->setProperty("kbIcon", QStringLiteral("trash"));
	wipe->setProperty("kbIconToken", QStringLiteral("danger"));
	connect(wipe, &QPushButton::clicked, this, &KaiboDock::confirmDeleteData);
	dataRow->addWidget(wipe);
	dataRow->addStretch(1);
	dc->addLayout(dataRow);
	col->addWidget(data);

	auto *about = card(page);
	auto *ab = new QVBoxLayout(about);
	ab->setContentsMargins(12, 12, 12, 12);
	ab->setSpacing(6);
	ab->addWidget(label(T("Settings.About"), "label", about));
	ab->addWidget(label(QStringLiteral("Kaibo %1").arg(QString::fromLatin1(PLUGIN_VERSION)), "strong", about));
	ab->addWidget(label(T("About.Disclaimer"), "caption", about));
	auto *project = button(T("About.Project"), "ghost", about);
	project->setProperty("kbSize", QStringLiteral("sm"));
	project->setProperty("kbIcon", QStringLiteral("external"));
	connect(project, &QPushButton::clicked, this,
		[] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kProjectUrl))); });
	ab->addWidget(project, 0, Qt::AlignLeft);
	col->addWidget(about);
	return page;
}

// ------------------------------------------------------------------ rendering

void KaiboDock::setSettingsOpen(bool open)
{
	m_settingsOpen = open;
	render();
	m_scroll->verticalScrollBar()->setValue(0);
}

void KaiboDock::render()
{
	m_rendering = true;
	const auto &s = m_controller->state();
	const Phase phase = m_controller->phase();

	// Page
	QWidget *page = m_loadingPage;
	if (m_settingsOpen)
		page = m_settingsPage;
	else if (s.auth == Auth::SignedOut)
		page = m_loginPage;
	else if (s.auth == Auth::SignedIn)
		page = m_mainPage;
	m_pages->setCurrentWidget(page);
	m_backButton->setVisible(m_settingsOpen);
	m_settingsButton->setVisible(!m_settingsOpen);

	// Header pill
	if (s.busy == Busy::GoingLive)
		m_pill->set(StatusPill::Tone::Busy, T("Status.Starting"));
	else if (s.busy == Busy::Ending)
		m_pill->set(StatusPill::Tone::Busy, T("Status.Stopping"));
	else if (s.auth != Auth::SignedIn)
		m_pill->set(StatusPill::Tone::Neutral,
			    s.auth == Auth::SignedOut ? T("Status.SignedOut") : T("Status.Connecting"));
	else
		switch (phase) {
		case Phase::Live:
			m_pill->set(StatusPill::Tone::Live, T("Status.Live"));
			break;
		case Phase::Reconnecting:
			m_pill->set(StatusPill::Tone::Warn, T("Status.Reconnecting"));
			break;
		case Phase::RoomOnly:
			m_pill->set(StatusPill::Tone::Warn, T("Status.RoomOnly"));
			break;
		case Phase::Dropped:
			m_pill->set(StatusPill::Tone::Warn, T("Status.Dropped"));
			break;
		case Phase::StreamOnly:
			m_pill->set(StatusPill::Tone::Warn, T("Status.StreamOnly"));
			break;
		case Phase::Offline:
			m_pill->set(StatusPill::Tone::Neutral,
				    s.roomId > 0 || !s.roomLoaded ? T("Status.Offline") : T("Status.NoRoom"));
			break;
		}

	// Login
	if (s.legacyAvailable && m_legacyBanner->isHidden())
		m_legacyBanner->present(Banner::Tone::Info, QStringLiteral("info"), T("Banner.Legacy"),
					T("Banner.Import"), T("Banner.Ignore"));
	else if (!s.legacyAvailable)
		m_legacyBanner->hide();
	if (m_picker->isVisible() && m_picker->isLoading() && !s.areasLoading)
		m_picker->updateData(s.areas, s.recentAreas, s.areaDraft, false);
	if (page == m_loginPage && isVisible() && s.qr == QrPhase::Idle)
		m_controller->startQrLogin();
	if (page != m_loginPage && s.auth == Auth::SignedIn && s.qr != QrPhase::Idle)
		m_controller->stopQrLogin();

	// Account
	m_name->setText(s.profile.name.empty() ? T("Header.Unknown") : qs(s.profile.name));
	m_avatar->setName(qs(s.profile.name));
	m_avatar->setLive(s.room.live);
	if (s.roomId > 0)
		m_roomLabel->setText(T("Header.Room").arg(s.roomId));
	else
		m_roomLabel->setText(s.roomLoaded || s.profile.mid ? T("Header.NoRoom") : QString());
	const UiLocale locale = uiLocale();
	m_followers->setText(T("Stat.Fans").arg(qs(formatCount(s.room.followers, locale))));
	m_online->setText(T("Stat.Online").arg(qs(formatCount(s.room.online, locale))));
	m_online->parentWidget()->setVisible(s.room.live);
	m_stats->setVisible(s.roomLoaded);

	// Room fields — never overwrite what the person is typing.
	if (!m_title->hasFocus() && m_title->text() != s.titleDraft)
		m_title->setText(s.titleDraft);
	const int titleLength = static_cast<int>(utf8Length(ss(s.titleDraft.trimmed())));
	m_titleCounter->setText(QStringLiteral("%1/%2").arg(titleLength).arg(LiveController::kTitleLimit));
	const bool tooLong = m_controller->titleTooLong();
	Theme::setProp(m_title, "kbError", tooLong);
	Theme::setProp(m_titleCounter, "kbText", tooLong ? QStringLiteral("danger") : QStringLiteral("faint"));
	m_titleError->setText(T("Msg.TitleTooLong").arg(LiveController::kTitleLimit));
	m_titleError->setVisible(tooLong);

	QString areaText = T("Room.PickArea");
	if (const Area *area = findArea(s.areas, s.areaDraft))
		areaText = qs(area->parentName) + QStringLiteral("  ›  ") + qs(area->name);
	else if (s.areaDraft > 0 && s.areaDraft == s.room.areaId && !s.room.areaName.empty())
		areaText = qs(s.room.parentAreaName) + QStringLiteral("  ›  ") + qs(s.room.areaName);
	m_areaText->setText(areaText);

	m_newsToggle->setVisible(s.newsLoaded);
	m_newsToggle->setText(s.news.empty() && s.newsDraft.isEmpty() ? T("Room.NewsAdd") : T("Room.NewsEdit"));
	if (!m_news->hasFocus() && m_news->toPlainText() != s.newsDraft)
		m_news->setPlainText(s.newsDraft);
	const int newsLength = static_cast<int>(utf8Length(ss(s.newsDraft.trimmed())));
	m_newsCounter->setText(QStringLiteral("%1/%2").arg(newsLength).arg(LiveController::kNewsLimit));
	const bool newsOpen = m_newsToggle->isChecked() && s.newsLoaded;
	m_news->setVisible(newsOpen);
	m_newsCounter->setVisible(newsOpen);
	Theme::setProp(m_news, "kbError", newsLength > LiveController::kNewsLimit);

	const bool unsynced = m_controller->hasUnsyncedChanges();
	m_sync->setEnabled(unsynced && s.busy == Busy::None && !tooLong && newsLength <= LiveController::kNewsLimit);
	m_sync->setText(s.busy == Busy::Syncing ? T("Room.Saving") : unsynced ? T("Room.Save") : T("Room.Saved"));
	m_syncHint->setText(unsynced ? T("Room.Unsaved") : QString());

	renderBanner();
	renderGoLive();
	renderClock();

	m_streamCard->setVisible(!s.key.isEmpty());
	m_server->setText(s.server);
	m_key->setText(s.key);

	// Settings
	const QSignalBlocker b1(m_optAutoStart), b2(m_optCloseOnStop), b3(m_optCloseOnExit), b4(m_optRestore),
		b5(m_optConfirm);
	m_optAutoStart->setChecked(s.options.autoStartStream);
	m_optCloseOnStop->setChecked(s.options.closeRoomOnStreamStop);
	m_optCloseOnExit->setChecked(s.options.closeRoomOnExit);
	m_optRestore->setChecked(s.options.restoreService);
	m_optConfirm->setChecked(s.options.confirmEnd);
	m_accountLabel->setText(
		s.auth == Auth::SignedIn
			? T("Settings.SignedInAs").arg(qs(s.profile.name.empty() ? std::string("?") : s.profile.name))
			: T("Settings.NotSignedIn"));
	m_signOut->setVisible(s.auth == Auth::SignedIn);

	m_rendering = false;
}

void KaiboDock::renderBanner()
{
	const auto &s = m_controller->state();
	disconnect(m_banner, &Banner::primaryClicked, nullptr, nullptr);
	disconnect(m_banner, &Banner::secondaryClicked, nullptr, nullptr);
	auto on = [this](auto primary, auto secondary) {
		connect(m_banner, &Banner::primaryClicked, this, primary);
		connect(m_banner, &Banner::secondaryClicked, this, secondary);
	};
	auto none = [] {
	};

	switch (m_controller->phase()) {
	case Phase::Dropped:
		m_banner->present(Banner::Tone::Error, QStringLiteral("signal"), T("Banner.Dropped"),
				  T("Banner.Restream"), T("Banner.CloseRoom"));
		on([this] { m_controller->startObsStream(); }, [this] { m_controller->closeRoom(); });
		return;
	case Phase::RoomOnly:
		if (s.busy == Busy::None) {
			m_banner->present(Banner::Tone::Warn, QStringLiteral("alert"), T("Banner.RoomOnly"),
					  T("Banner.StartStream"), T("Banner.CloseRoom"));
			on([this] { m_controller->startObsStream(); }, [this] { m_controller->closeRoom(); });
			return;
		}
		break;
	case Phase::StreamOnly:
		m_banner->present(Banner::Tone::Warn, QStringLiteral("alert"), T("Banner.StreamOnly"),
				  T("Banner.OpenRoom"), T("Banner.StopStream"));
		on([this] { m_controller->goLive(); }, [this] { m_controller->stopObsStream(); });
		return;
	default:
		break;
	}
	if (s.offline) {
		m_banner->present(Banner::Tone::Error, QStringLiteral("alert"), T("Msg.Offline"), T("Loading.Retry"));
		on([this] { m_controller->refresh(); }, none);
		return;
	}
	if (s.profile.mid > 0 && s.roomId <= 0) {
		m_banner->present(Banner::Tone::Info, QStringLiteral("info"), T("Banner.NoRoom"),
				  T("Banner.OpenRoomCenter"));
		on([] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kLiveCenterUrl))); }, none);
		return;
	}
	m_banner->hide();
}

void KaiboDock::renderGoLive()
{
	const auto &s = m_controller->state();
	const Phase phase = m_controller->phase();
	const bool roomLive = s.room.live;
	QString state = QStringLiteral("ready"), text, hint, icon;
	bool enabled = true;

	if (s.busy == Busy::GoingLive) {
		state = QStringLiteral("busy");
		text = T("Button.Starting");
	} else if (s.busy == Busy::Ending) {
		state = QStringLiteral("busy");
		text = T("Button.Stopping");
	} else if (roomLive) {
		state = QStringLiteral("end");
		text = T("Button.End");
		hint = T("Hint.Live");
		icon = QStringLiteral("stop");
	} else if (s.roomId <= 0) {
		enabled = false;
		text = T("Status.NoRoom");
	} else if (s.areaDraft <= 0) {
		enabled = false;
		text = T("Hint.PickArea");
	} else {
		text = T("Button.Start");
		hint = phase == Phase::StreamOnly  ? T("Hint.StartWhileStreaming")
		       : s.options.autoStartStream ? T("Hint.Start")
						   : T("Hint.StartNoStream");
		icon = QStringLiteral("play");
	}
	m_goLive->setEnabled(enabled);
	Theme::setProp(m_goLive, "kbState", state);
	m_goLive->setText(text);
	m_goLive->setAccessibleName(text);
	m_goLive->setIcon(icon.isEmpty() ? QIcon()
					 : QIcon(theme().pixmap(icon,
								theme().color(state == QStringLiteral("end")
										      ? QStringLiteral("danger")
										      : QStringLiteral("on-brand")),
								20, devicePixelRatioF())));
	m_goLiveHint->setText(hint);
	m_goLiveHint->setVisible(!hint.isEmpty());
	m_clockRow->setVisible(roomLive && s.liveSince > 0);
	if (roomLive && s.liveSince > 0) {
		if (!m_clockTimer.isActive())
			m_clockTimer.start();
	} else {
		m_clockTimer.stop();
	}
}

void KaiboDock::renderClock()
{
	const auto &s = m_controller->state();
	if (s.liveSince <= 0)
		return;
	m_clock->setText(qs(formatClock(QDateTime::currentSecsSinceEpoch() - s.liveSince)));
}

void KaiboDock::renderQr()
{
	const auto &s = m_controller->state();
	switch (s.qr) {
	case QrPhase::Idle:
	case QrPhase::Loading:
		m_qr->setPayload({});
		m_qr->setOverlay(QrView::Overlay::Loading, T("Login.Loading"));
		m_qrTitle->setText(T("Login.Title"));
		m_qrHint->setText(T("Login.Hint"));
		break;
	case QrPhase::Waiting:
		m_qr->setPayload(s.qrUrl);
		m_qr->setOverlay(QrView::Overlay::None);
		m_qrTitle->setText(T("Login.Title"));
		m_qrHint->setText(T("Login.Hint"));
		break;
	case QrPhase::Scanned:
		m_qr->setOverlay(QrView::Overlay::Scanned, T("Login.ScannedSub"));
		m_qrTitle->setText(T("Login.Scanned"));
		m_qrHint->setText(T("Login.ScannedHint"));
		break;
	case QrPhase::Expired:
		m_qr->setOverlay(QrView::Overlay::Expired, T("Login.Refresh"));
		m_qrTitle->setText(T("Login.Expired"));
		m_qrHint->setText(T("Login.ExpiredHint"));
		break;
	case QrPhase::Failed:
		m_qr->setPayload({});
		m_qr->setOverlay(QrView::Overlay::Failed, T("Loading.Retry"));
		m_qrTitle->setText(T("Login.Failed"));
		m_qrHint->setText(s.qrError);
		break;
	}
}

// ------------------------------------------------------------------ theme

void KaiboDock::applyTheme()
{
	setStyleSheet(theme().styleSheet());
	m_logo->setPixmap(theme().pixmap(QStringLiteral("kaibo-mark"), QColor(), 18, devicePixelRatioF()));
	// Re-tint every icon for the new token values.
	for (QPushButton *b : findChildren<QPushButton *>()) {
		const QString icon = b->property("kbIcon").toString();
		if (icon.isEmpty())
			continue;
		const QString token = b->property("kbIconToken").toString();
		b->setIcon(theme().icon(icon, token.isEmpty() ? QStringLiteral("ink-muted") : token));
	}
	for (QLabel *l : findChildren<QLabel *>()) {
		const QString icon = l->property("kbIcon").toString();
		if (icon.isEmpty())
			continue;
		const QString token = l->property("kbIconToken").toString();
		l->setPixmap(theme().pixmap(icon, theme().color(token), l->width(), devicePixelRatioF()));
	}
	m_banner->refreshIcon();
	m_legacyBanner->refreshIcon();
	renderGoLive();
	renderQr();
	update();
}

void KaiboDock::changeEvent(QEvent *event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange ||
	    event->type() == QEvent::ThemeChange) {
		if (theme().followPalette(QApplication::palette()))
			applyTheme();
	}
}

void KaiboDock::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	if (m_toast->isVisible())
		m_toast->reposition();
}

void KaiboDock::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	if (theme().followPalette(QApplication::palette()))
		applyTheme();
	render();
	if (m_controller->state().auth == Auth::SignedIn)
		m_controller->refresh();
}

void KaiboDock::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);
	if (m_controller->state().auth == Auth::SignedOut)
		m_controller->stopQrLogin(); // no polling while nobody can see the code
	if (m_keyReveal->isChecked())
		m_keyReveal->setChecked(false); // re-mask the stream key
}

// ------------------------------------------------------------------ actions

void KaiboDock::requestGoLive()
{
	const Phase phase = m_controller->phase();
	if (phase == Phase::Offline || phase == Phase::StreamOnly)
		m_controller->goLive();
}

void KaiboDock::requestEndLive()
{
	const auto &s = m_controller->state();
	if (!s.room.live || s.busy != Busy::None)
		return;
	if (s.options.confirmEnd) {
		QMessageBox box(QMessageBox::Question, T("Confirm.EndTitle"), T("Confirm.EndText"),
				QMessageBox::NoButton, this);
		QPushButton *end = box.addButton(T("Button.End"), QMessageBox::DestructiveRole);
		box.addButton(T("Common.Cancel"), QMessageBox::RejectRole);
		box.setDefaultButton(end);
		box.exec();
		if (box.clickedButton() != end)
			return;
	}
	m_controller->endLive();
}

void KaiboDock::openAreaPicker()
{
	m_controller->ensureAreas();
	const auto &s = m_controller->state();
	m_picker->open(m_areaButton, s.areas, s.recentAreas, s.areaDraft, s.areasLoading);
}

void KaiboDock::showTitleHistory()
{
	const auto &titles = m_controller->state().recentTitles;
	QMenu menu(this);
	if (titles.empty()) {
		menu.addAction(T("Room.NoHistory"))->setEnabled(false);
	} else {
		for (const auto &title : titles) {
			const QString t = qs(title);
			QAction *action = menu.addAction(t);
			connect(action, &QAction::triggered, this, [this, t] {
				m_title->setText(t);
				m_controller->setTitleDraft(t);
			});
		}
	}
	menu.exec(QCursor::pos());
}

void KaiboDock::showAccountMenu()
{
	const auto &s = m_controller->state();
	QMenu menu(this);
	connect(menu.addAction(theme().icon(QStringLiteral("external")), T("Menu.OpenCenter")), &QAction::triggered,
		this, [] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kLiveCenterUrl))); });
	if (s.roomId > 0)
		connect(menu.addAction(theme().icon(QStringLiteral("copy")), T("Menu.CopyRoomLink")),
			&QAction::triggered, this, [this, id = s.roomId] { copyText(roomLink(id)); });
	connect(menu.addAction(theme().icon(QStringLiteral("refresh")), T("Menu.Refresh")), &QAction::triggered,
		m_controller, &LiveController::refresh);
	menu.addSeparator();
	connect(menu.addAction(theme().icon(QStringLiteral("settings")), T("Settings.Title")), &QAction::triggered,
		this, [this] { setSettingsOpen(true); });
	menu.exec(QCursor::pos());
}

void KaiboDock::showFaceAuth(const QString &url)
{
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("KaiboDock"));
	dialog.setWindowTitle(T("Face.Title"));
	dialog.setStyleSheet(theme().styleSheet());
	auto *col = new QVBoxLayout(&dialog);
	col->setContentsMargins(24, 24, 24, 20);
	col->setSpacing(12);
	auto *heading = label(T("Face.Heading"), "title", &dialog);
	heading->setAlignment(Qt::AlignCenter);
	col->addWidget(heading);
	auto *hint = label(T("Face.Hint"), "caption", &dialog);
	hint->setAlignment(Qt::AlignCenter);
	col->addWidget(hint);
	auto *qr = new QrView(&dialog);
	qr->setPayload(url);
	qr->setOverlay(QrView::Overlay::None);
	col->addWidget(qr, 0, Qt::AlignHCenter);
	auto *row = new QHBoxLayout();
	auto *browser = button(T("Face.OpenInBrowser"), "ghost", &dialog);
	connect(browser, &QPushButton::clicked, &dialog, [url] { QDesktopServices::openUrl(QUrl(url)); });
	auto *done = button(T("Face.Done"), "primary", &dialog);
	connect(done, &QPushButton::clicked, &dialog, &QDialog::accept);
	row->addWidget(browser);
	row->addStretch(1);
	row->addWidget(done);
	col->addLayout(row);
	if (dialog.exec() == QDialog::Accepted)
		m_controller->goLive();
}

void KaiboDock::confirmDeleteData()
{
	QMessageBox box(QMessageBox::Warning, T("Settings.DeleteData"),
			T("Confirm.DeleteData").arg(m_controller->storageDirectory()), QMessageBox::NoButton, this);
	QPushButton *wipe = box.addButton(T("Settings.DeleteData"), QMessageBox::DestructiveRole);
	box.addButton(T("Common.Cancel"), QMessageBox::RejectRole);
	box.exec();
	if (box.clickedButton() != wipe)
		return;
	QString error;
	if (m_controller->deleteAllData(error))
		m_toast->showMessage(0, T("Msg.DataDeleted"), {});
	else
		m_toast->showMessage(2, T("Msg.DataDeleteFailed"), error);
	setSettingsOpen(false);
}

void KaiboDock::copyText(const QString &text)
{
	if (text.isEmpty())
		return;
	QApplication::clipboard()->setText(text);
	m_toast->showMessage(0, T("Msg.Copied"), {});
}

void KaiboDock::readOptions()
{
	if (m_rendering)
		return;
	Options o;
	o.autoStartStream = m_optAutoStart->isChecked();
	o.closeRoomOnStreamStop = m_optCloseOnStop->isChecked();
	o.closeRoomOnExit = m_optCloseOnExit->isChecked();
	o.restoreService = m_optRestore->isChecked();
	o.confirmEnd = m_optConfirm->isChecked();
	m_controller->setOptions(o);
}

} // namespace kaibo::ui
