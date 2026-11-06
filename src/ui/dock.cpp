// SPDX-License-Identifier: GPL-2.0-only
#include "ui/dock.hpp"
#include "app/controller.hpp"
#include "app/i18n.hpp"
#include "app/secret_store.hpp"
#include "core/face_auth.hpp"
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
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>

namespace kaibo::ui {
namespace {

using Phase = LiveController::Phase;
using Busy = LiveController::Busy;
using Auth = LiveController::Auth;
using QrPhase = LiveController::QrPhase;
using FaceAuth = LiveController::FaceAuth;

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

// The controller sends LiveController::ToastKind as an int over the signal; map
// it to the typed UI enum so no magic 0/1/2 ever reaches the toast.
ToastKind toToastKind(int kind)
{
	switch (static_cast<LiveController::ToastKind>(kind)) {
	case LiveController::ToastKind::Ok:
		return ToastKind::Ok;
	case LiveController::ToastKind::Error:
		return ToastKind::Error;
	case LiveController::ToastKind::Info:
		break;
	}
	return ToastKind::Info;
}

} // namespace

KaiboDock::KaiboDock(LiveController *controller, QWidget *parent) : QWidget(parent), m_controller(controller)
{
	setObjectName(QStringLiteral("KaiboDock"));
	setAttribute(Qt::WA_StyledBackground);
	// Load the design tokens before reading any length out of them.
	theme().load();
	theme().followPalette(QApplication::palette());
	setMinimumWidth(theme().length(QStringLiteral("dock-min"), 280));

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
	// Optional OBS 33 layout hint, above every page; shown by updateLayoutHint().
	m_hintWrap = new QWidget(content);
	auto *hintLayout = new QVBoxLayout(m_hintWrap);
	const int hintMargin = theme().length(QStringLiteral("space-3"), 12);
	hintLayout->setContentsMargins(0, 0, 0, hintMargin);
	hintLayout->setSpacing(0);
	m_hintBanner = new Banner(m_hintWrap);
	connect(m_hintBanner, &Banner::secondaryClicked, this, &KaiboDock::dismissLayoutHint);
	hintLayout->addWidget(m_hintBanner);
	m_hintWrap->hide();
	col->addWidget(m_hintWrap);
	// Pages share one column and only the current one is visible. A QStackedWidget would size the
	// scroll content to its tallest page (its heightForWidth spans hidden pages too).
	m_loadingPage = buildLoadingPage();
	m_loginPage = buildLoginPage();
	m_mainPage = buildMainPage();
	m_settingsPage = buildSettingsPage();
	for (QWidget *page : {m_loadingPage, m_loginPage, m_mainPage, m_settingsPage}) {
		page->hide();
		col->addWidget(page);
	}
	col->addStretch(1);
	m_scroll->setWidget(content);
	root->addWidget(m_scroll, 1);
	m_actionBar = buildActionBar();

	m_picker = new AreaPicker(this);
	connect(m_picker, &AreaPicker::picked, m_controller, &LiveController::setAreaDraft);
	connect(m_picker, &AreaPicker::retryRequested, m_controller, &LiveController::ensureAreas);

	// The toast is a normal row under the scroll area: it takes layout space
	// instead of painting over the Go Live button, and cannot swallow its clicks.
	m_toastWrap = new QWidget(this);
	auto *toastLayout = new QVBoxLayout(m_toastWrap);
	const int toastMargin = theme().length(QStringLiteral("space-3"), 12);
	toastLayout->setContentsMargins(toastMargin, 0, toastMargin, toastMargin);
	m_toast = new Toast(m_toastWrap);
	toastLayout->addWidget(m_toast);
	m_toastWrap->hide();
	root->addWidget(m_toastWrap);
	root->addWidget(m_actionBar);
	connect(m_toast, &Toast::visibilityChanged, this, [this] {
		if (m_toast->isVisible())
			m_toastWrap->show();
		else
			m_toastWrap->hide();
	});

	m_clockTimer.setInterval(1000);
	connect(&m_clockTimer, &QTimer::timeout, this, &KaiboDock::renderClock);

	// changed() fires on every keystroke in the title/announcement editors; collapse
	// bursts into one render pass on the next event-loop turn.
	m_renderTimer.setSingleShot(true);
	m_renderTimer.setInterval(0);
	connect(&m_renderTimer, &QTimer::timeout, this, &KaiboDock::render);
	connect(m_controller, &LiveController::changed, this, [this] { scheduleRender(); });
	connect(m_controller, &LiveController::qrChanged, this, &KaiboDock::renderQr);
	connect(m_controller, &LiveController::toast, this,
		[this](const QString &text, int kind, const QString &detail) {
			m_toastWrap->show();
			m_toast->showMessage(toToastKind(kind), text, detail);
		});

	// Everything reachable with the mouse is reachable from the keyboard: Ctrl+Enter
	// goes live from anywhere in the dock, Esc leaves the settings page.
	auto *goLiveShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
	goLiveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(goLiveShortcut, &QShortcut::activated, this, &KaiboDock::requestGoLive);
	auto *escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
	escapeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(escapeShortcut, &QShortcut::activated, this, [this] {
		if (m_settingsOpen)
			setSettingsOpen(false);
	});

	// Re-elide labels after a resize settles; the single-shot timer coalesces the
	// bursts produced while the dock separator is dragged.
	m_elideTimer.setSingleShot(true);
	m_elideTimer.setInterval(0);
	connect(&m_elideTimer, &QTimer::timeout, this, &KaiboDock::updateElidedTexts);

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

	m_loginBanner = new Banner(page);
	connect(m_loginBanner, &Banner::primaryClicked, m_controller, &LiveController::refresh);
	col->addWidget(m_loginBanner);

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
	// Long names/labels elide (ElideRight) with the full value in the tooltip and
	// accessible name instead of forcing the card wider than the dock.
	m_name->setMinimumSize(1, 1);
	m_roomLabel = label({}, "caption", account);
	m_roomLabel->setWordWrap(false);
	m_roomLabel->setMinimumSize(1, 1);
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
	m_moreButton = iconButton(QStringLiteral("more"), T("Common.More"), account);
	connect(m_moreButton, &QPushButton::clicked, this, &KaiboDock::showAccountMenu);
	who->addWidget(m_moreButton, 0, Qt::AlignTop);
	ac->addLayout(who);
	m_stats = new QWidget(account);
	auto *stats = new QHBoxLayout(m_stats);
	stats->setContentsMargins(0, 0, 0, 0);
	stats->setSpacing(12);
	stats->addWidget(iconText(QStringLiteral("heart"), &m_followers, m_stats));
	stats->addStretch(1);
	ac->addWidget(m_stats);
	ac->addWidget(divider(account));

	// Room: same card, so the column reads as one form instead of a stack of boxes.
	QFrame *room = account;
	QVBoxLayout *rc = ac;
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
	m_titleHistoryButton = iconButton(QStringLiteral("history"), T("Room.TitleHistory"), room);
	connect(m_titleHistoryButton, &QPushButton::clicked, this, &KaiboDock::showTitleHistory);
	titleRow->addWidget(m_titleHistoryButton);
	rc->addLayout(titleRow);
	m_titleError = label({}, "danger", room);
	m_titleError->setWordWrap(true);
	rc->addWidget(m_titleError);

	rc->addSpacing(6);
	rc->addWidget(label(T("Room.Area"), "strong", room));
	m_areaButton = button({}, "select", room);
	m_areaButton->setAccessibleName(T("Room.Area"));
	auto *areaInner = new QHBoxLayout(m_areaButton);
	const int areaInset = theme().length(QStringLiteral("space-2"), 8);
	areaInner->setContentsMargins(areaInset, 0, areaInset, 0);
	m_areaText = new QLabel(m_areaButton);
	m_areaText->setMinimumSize(1, 1);
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
	// Explains a failed area load without making the person open the picker first.
	m_areaHint = label({}, "caption", room);
	m_areaHint->setVisible(false);
	m_areaHint->setWordWrap(true);
	rc->addWidget(m_areaHint);

	rc->addSpacing(6);
	auto *newsHead = new QHBoxLayout();
	m_newsToggle = button(T("Room.NewsAdd"), "ghost", room);
	m_newsToggle->setProperty("kbSize", QStringLiteral("sm"));
	m_newsToggle->setProperty("kbIcon", QStringLiteral("megaphone"));
	m_newsToggle->setAccessibleName(T("Room.News"));
	m_newsToggle->setCheckable(true);
	newsHead->addWidget(m_newsToggle);
	newsHead->addStretch(1);
	m_newsCounter = label({}, "faint", room);
	m_newsCounter->setWordWrap(false);
	newsHead->addWidget(m_newsCounter);
	rc->addLayout(newsHead);
	m_newsHint = label({}, "caption", room);
	m_newsHint->setWordWrap(true);
	m_newsHint->setVisible(false);
	rc->addWidget(m_newsHint);
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
		if (m_rendering)
			return;
		m_newsExpanded = on;
		render(); // only the visibility/text of the announcement block changes
		if (on && m_news->isVisible())
			m_news->setFocus();
	});
	rc->addWidget(m_news);
	m_newsError = label({}, "danger", room);
	m_newsError->setWordWrap(true);
	m_newsError->setVisible(false);
	rc->addWidget(m_newsError);

	rc->addSpacing(4);
	auto *syncRow = new QHBoxLayout();
	m_syncHint = label({}, "caption", room);
	syncRow->addWidget(m_syncHint, 1);
	m_sync = button(T("Room.Save"), nullptr, room);
	m_sync->setProperty("kbSize", QStringLiteral("sm"));
	connect(m_sync, &QPushButton::clicked, m_controller, &LiveController::syncRoom);
	syncRow->addWidget(m_sync, 0, Qt::AlignTop);
	rc->addLayout(syncRow);
	col->addWidget(account);

	// Face verification card — only rendered while Bilibili asks for one.
	m_faceCard = buildFaceAuthCard();
	col->addWidget(m_faceCard);

	// Stream info (only after Kaibo opened the room in this session). OBS already has
	// these values, so they stay folded away until someone needs to copy them.
	m_streamCard = card(page);
	auto *streamCol = new QVBoxLayout(m_streamCard);
	streamCol->setContentsMargins(4, 4, 4, 4);
	streamCol->setSpacing(0);
	m_streamToggle = button({}, "disclosure", m_streamCard);
	m_streamToggle->setCheckable(true);
	m_streamToggle->setAccessibleName(T("Info.Title"));
	auto *streamHead = new QHBoxLayout(m_streamToggle);
	const int streamInset = theme().length(QStringLiteral("space-2"), 8);
	streamHead->setContentsMargins(streamInset, 0, streamInset, 0);
	auto *streamTitle = label(T("Info.Title"), "label", m_streamToggle);
	streamTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
	streamHead->addWidget(streamTitle, 1);
	m_streamChevron = new QLabel(m_streamToggle);
	m_streamChevron->setProperty("kbIcon", QStringLiteral("chevron-down"));
	m_streamChevron->setProperty("kbIconToken", QStringLiteral("ink-muted"));
	m_streamChevron->setFixedSize(16, 16);
	m_streamChevron->setScaledContents(true);
	m_streamChevron->setAttribute(Qt::WA_TransparentForMouseEvents);
	streamHead->addWidget(m_streamChevron);
	streamCol->addWidget(m_streamToggle);
	m_streamBody = new QWidget(m_streamCard);
	auto *sc = new QVBoxLayout(m_streamBody);
	sc->setContentsMargins(streamInset, 4, streamInset, streamInset);
	sc->setSpacing(6);
	m_streamBody->hide();
	streamCol->addWidget(m_streamBody);
	connect(m_streamToggle, &QPushButton::toggled, this, [this](bool on) {
		m_streamBody->setVisible(on);
		const QString icon = on ? QStringLiteral("chevron-up") : QStringLiteral("chevron-down");
		m_streamChevron->setProperty("kbIcon", icon); // applyTheme() re-tints by this name
		m_streamChevron->setPixmap(
			theme().pixmap(icon, theme().color(QStringLiteral("ink-muted")), 16, devicePixelRatioF()));
		if (!on && m_keyReveal->isChecked())
			m_keyReveal->setChecked(false); // never leave the key showing behind a closed fold
	});
	sc->addWidget(label(T("Info.Server"), "strong", m_streamBody));
	auto *serverRow = new QHBoxLayout();
	m_server = new QLineEdit(m_streamBody);
	m_server->setReadOnly(true);
	m_server->setFont(theme().monoFont());
	m_server->setAccessibleName(T("Info.Server"));
	serverRow->addWidget(m_server, 1);
	auto *copyServer = iconButton(QStringLiteral("copy"), T("Common.Copy"), m_streamBody);
	connect(copyServer, &QPushButton::clicked, this, [this] { copyText(m_server->text(), T("Info.Server")); });
	serverRow->addWidget(copyServer);
	sc->addLayout(serverRow);
	sc->addWidget(label(T("Info.Key"), "strong", m_streamBody));
	auto *keyRow = new QHBoxLayout();
	m_key = new QLineEdit(m_streamBody);
	m_key->setReadOnly(true);
	m_key->setEchoMode(QLineEdit::Password);
	m_key->setFont(theme().monoFont());
	m_key->setAccessibleName(T("Info.Key"));
	keyRow->addWidget(m_key, 1);
	m_keyReveal = iconButton(QStringLiteral("eye"), T("Info.Show"), m_streamBody);
	m_keyReveal->setCheckable(true);
	connect(m_keyReveal, &QPushButton::toggled, this, [this](bool on) {
		m_key->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
		m_keyReveal->setProperty("kbIcon", on ? QStringLiteral("eye-off") : QStringLiteral("eye"));
		m_keyReveal->setIcon(
			theme().icon(m_keyReveal->property("kbIcon").toString(), QStringLiteral("ink-muted"), 16));
		m_keyReveal->setToolTip(on ? T("Info.Hide") : T("Info.Show"));
	});
	keyRow->addWidget(m_keyReveal);
	auto *copyKey = iconButton(QStringLiteral("copy"), T("Common.Copy"), m_streamBody);
	connect(copyKey, &QPushButton::clicked, this, [this] { copyText(m_key->text(), T("Info.Key")); });
	keyRow->addWidget(copyKey);
	sc->addLayout(keyRow);
	sc->addWidget(label(T("Info.Hint"), "caption", m_streamBody));
	col->addWidget(m_streamCard);

	return page;
}

// Pinned under the scroll area, so the one button that matters is never scrolled away
// and the broadcast's state can be read from across the room: while on air the whole bar
// takes the brand colour, and a broadcast that needs attention turns it to warning.
QWidget *KaiboDock::buildActionBar()
{
	auto *bar = new QFrame(this);
	bar->setObjectName(QStringLiteral("KbActionBar"));
	auto *col = new QVBoxLayout(bar);
	const int inset = theme().length(QStringLiteral("space-3"), 12);
	col->setContentsMargins(inset, inset, inset, inset);
	col->setSpacing(theme().length(QStringLiteral("space-2"), 8));

	m_clockRow = new QWidget(bar);
	auto *clockRow = new QHBoxLayout(m_clockRow);
	clockRow->setContentsMargins(2, 0, 2, 0);
	clockRow->addWidget(iconText(QStringLiteral("users"), &m_online, m_clockRow));
	clockRow->addStretch(1);
	m_clock = label(QStringLiteral("00:00:00"), "timer", m_clockRow);
	m_clock->setToolTip(T("Live.Duration"));
	m_clock->setAccessibleName(T("Live.Duration"));
	QFont clockFont = m_clock->font();
	clockFont.setStyleHint(QFont::Monospace);
	m_clock->setFont(clockFont);
	clockRow->addWidget(m_clock);
	col->addWidget(m_clockRow);

	m_goLive = new QPushButton(bar);
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
	col->addWidget(m_goLive);
	m_goLiveHint = label({}, "caption", bar);
	m_goLiveHint->setAlignment(Qt::AlignCenter);
	col->addWidget(m_goLiveHint);
	return bar;
}

// Inline face-verification prompt: the code lives in the main column next to the Go
// Live button, so the dock stays usable and there is exactly one prompt per attempt.
QWidget *KaiboDock::buildFaceAuthCard()
{
	auto *c = card(this);
	auto *col = new QVBoxLayout(c);
	col->setContentsMargins(16, 16, 16, 14);
	col->setSpacing(10);

	m_faceHeading = label(T("Face.Heading"), "title", c);
	m_faceHeading->setAlignment(Qt::AlignCenter);
	m_faceHeading->setWordWrap(true);
	col->addWidget(m_faceHeading);
	m_faceHint = label(T("Face.Hint"), "caption", c);
	m_faceHint->setAlignment(Qt::AlignCenter);
	col->addWidget(m_faceHint);
	m_faceQr = new QrView(c);
	m_faceQr->setAccessibleName(T("Face.QrAccessible"));
	col->addWidget(m_faceQr, 0, Qt::AlignHCenter);

	m_faceCancel = button(T("Common.Cancel"), "ghost", c);
	m_faceCancel->setProperty("kbSize", QStringLiteral("sm"));
	connect(m_faceCancel, &QPushButton::clicked, m_controller, &LiveController::cancelFaceAuth);
	m_faceBrowser = button(T("Face.OpenInBrowser"), "ghost", c);
	m_faceBrowser->setProperty("kbSize", QStringLiteral("sm"));
	connect(m_faceBrowser, &QPushButton::clicked, this, [this] {
		const QString url = m_controller->state().faceAuthUrl;
		if (!url.isEmpty())
			QDesktopServices::openUrl(QUrl(url));
	});
	m_faceConfirm = button(T("Face.Done"), "primary", c);
	m_faceConfirm->setProperty("kbSize", QStringLiteral("sm"));
	connect(m_faceConfirm, &QPushButton::clicked, m_controller, &LiveController::confirmFaceAuth);
	// The two secondary actions share a row; the primary action spans the card, so
	// no label is clipped at the 280px dock minimum.
	auto *row = new QHBoxLayout();
	row->setSpacing(theme().length(QStringLiteral("space-2"), 8));
	row->addWidget(m_faceCancel);
	row->addWidget(m_faceBrowser);
	row->addStretch(1);
	col->addLayout(row);
	col->addWidget(m_faceConfirm);
	c->hide();
	return c;
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
	lc->addWidget(divider(live));
	lc->addSpacing(8);
	lc->addWidget(label(T("Settings.Hotkeys"), "caption", live));
	col->addWidget(live);

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
	m_dataPathFull = m_controller->storageDirectory();
	m_dataPath = label(m_dataPathFull, "faint", data);
	// A Windows path has no break opportunities: elide it in the middle (see
	// updateElidedTexts) instead of letting it widen the scroll content.
	m_dataPath->setWordWrap(false);
	m_dataPath->setMinimumSize(1, 1);
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
	if (m_settingsOpen == open)
		return;
	m_settingsOpen = open;
	render();
	m_scroll->verticalScrollBar()->setValue(0);
	// Keyboard users land on the header action that got them here.
	(open ? m_backButton : m_settingsButton)->setFocus(Qt::OtherFocusReason);
}

void KaiboDock::scheduleRender()
{
	// A hidden dock does not paint, so do not run a state pass for it either;
	// showEvent() renders the current state when it comes back.
	if (!isVisible())
		return;
	if (m_renderTimer.isActive())
		return;
	m_renderTimer.start();
}

void KaiboDock::render()
{
	if (m_rendering)
		return; // side effects inside the pass must never recurse into it
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
	for (QWidget *w : {m_loadingPage, m_loginPage, m_mainPage, m_settingsPage})
		w->setVisible(w == page);
	m_actionBar->setVisible(page == m_mainPage);
	m_backButton->setVisible(m_settingsOpen);
	m_settingsButton->setVisible(!m_settingsOpen);

	// Header pill
	if (s.busy == Busy::GoingLive)
		m_pill->set(StatusPill::Tone::Busy, T("Status.Starting"));
	else if (s.busy == Busy::Ending)
		m_pill->set(StatusPill::Tone::Busy, T("Status.Stopping"));
	else if (s.busy == Busy::Syncing)
		m_pill->set(StatusPill::Tone::Busy, T("Status.Saving"));
	else if (s.face != FaceAuth::None)
		m_pill->set(StatusPill::Tone::Busy, T("Status.FaceAuth"));
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
	// Offline on the sign-in page: say so instead of showing only a failed QR.
	if (page == m_loginPage && s.offline) {
		if (m_loginBanner->isHidden())
			m_loginBanner->present(Banner::Tone::Error, QStringLiteral("alert"), T("Msg.Offline"),
					       T("Loading.Retry"));
	} else {
		m_loginBanner->hide();
	}
	if (m_picker->isVisible() && m_picker->needsUpdate(s.areas, s.recentAreas, s.areaDraft, s.areasLoading))
		m_picker->updateData(s.areas, s.recentAreas, s.areaDraft, s.areasLoading);
	if (page == m_loginPage && isVisible() && s.qr == QrPhase::Idle)
		m_controller->startQrLogin();
	if (page != m_loginPage && s.auth == Auth::SignedIn && s.qr != QrPhase::Idle)
		m_controller->stopQrLogin();

	// Account — pushed from state on every pass: resetSession() does not re-emit
	// avatarChanged, so a stale avatar must not survive a sign-out/sign-in.
	m_nameFull = s.profile.name.empty() ? T("Header.Unknown") : qs(s.profile.name);
	m_avatar->setImage(s.avatar);
	m_avatar->setName(m_nameFull);
	m_avatar->setLive(s.room.live);
	if (s.roomId > 0)
		m_roomLabelFull = T("Header.Room").arg(s.roomId);
	else
		m_roomLabelFull = s.roomLoaded || s.profile.mid ? T("Header.NoRoom") : QString();
	if (m_name->toolTip() != m_nameFull) {
		m_name->setToolTip(m_nameFull);
		m_name->setAccessibleName(m_nameFull);
	}
	if (m_roomLabel->toolTip() != m_roomLabelFull) {
		m_roomLabel->setToolTip(m_roomLabelFull);
		m_roomLabel->setAccessibleName(m_roomLabelFull);
	}
	const UiLocale locale = uiLocale();
	m_followers->setText(T("Stat.Fans").arg(qs(formatCount(s.room.followers, locale))));
	m_online->setText(T("Stat.Online").arg(qs(formatCount(s.room.online, locale))));
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
	bool areaPicked = false;
	if (const Area *area = findArea(s.areas, s.areaDraft)) {
		areaText = qs(area->parentName) + QStringLiteral("  ›  ") + qs(area->name);
		areaPicked = true;
	} else if (s.areaDraft > 0 && s.areaDraft == s.room.areaId && !s.room.areaName.empty()) {
		areaText = qs(s.room.parentAreaName) + QStringLiteral("  ›  ") + qs(s.room.areaName);
		areaPicked = true;
	}
	m_areaFull = areaText;
	const QString areaTip = areaPicked ? areaText : T("Room.PickArea");
	if (m_areaText->toolTip() != areaTip)
		m_areaText->setToolTip(areaTip);
	// The placeholder is a hint, not a selection: keep it muted so it does not read as picked.
	Theme::setProp(m_areaText, "kbText", areaPicked ? QVariant() : QVariant(QStringLiteral("faint")));
	const bool areaUnavailable = s.roomLoaded && !s.areasLoading && s.areas.empty();
	m_areaHint->setText(areaUnavailable ? T("Area.Unavailable") : QString());
	m_areaHint->setVisible(areaUnavailable);

	// The announcement row stays visible when the fetch failed: it explains itself
	// instead of silently disappearing. isChecked() is not UI state — m_newsExpanded is.
	if (!s.newsLoaded)
		m_newsExpanded = false;
	m_newsToggle->setVisible(true);
	m_newsToggle->setEnabled(s.newsLoaded);
	m_newsToggle->setText(s.news.empty() && s.newsDraft.isEmpty() ? T("Room.NewsAdd") : T("Room.NewsEdit"));
	m_newsHint->setText(s.newsLoaded ? QString() : T("Msg.NewsUnavailable"));
	m_newsHint->setVisible(!s.newsLoaded);
	{
		const QSignalBlocker blocker(m_newsToggle);
		m_newsToggle->setChecked(m_newsExpanded);
	}
	if (!m_news->hasFocus() && m_news->toPlainText() != s.newsDraft)
		m_news->setPlainText(s.newsDraft);
	const int newsLength = static_cast<int>(utf8Length(ss(s.newsDraft.trimmed())));
	m_newsCounter->setText(QStringLiteral("%1/%2").arg(newsLength).arg(LiveController::kNewsLimit));
	const bool newsOpen = m_newsExpanded && s.newsLoaded;
	m_news->setVisible(newsOpen);
	m_newsCounter->setVisible(newsOpen);
	const bool newsTooLong = newsLength > LiveController::kNewsLimit;
	Theme::setProp(m_news, "kbError", newsTooLong);
	Theme::setProp(m_newsCounter, "kbText", newsTooLong ? QStringLiteral("danger") : QStringLiteral("faint"));
	m_newsError->setText(T("Msg.NewsTooLong").arg(LiveController::kNewsLimit));
	m_newsError->setVisible(newsOpen && newsTooLong);

	const bool unsynced = m_controller->hasUnsyncedChanges();
	m_sync->setEnabled(unsynced && s.busy == Busy::None && !tooLong && newsLength <= LiveController::kNewsLimit);
	m_sync->setText(s.busy == Busy::Syncing ? T("Room.Saving") : unsynced ? T("Room.Save") : T("Room.Saved"));
	m_syncHint->setText(unsynced ? T("Room.Unsaved") : QString());

	renderBanner();
	renderFaceAuth();
	renderGoLive();
	renderClock();

	if (s.key.isEmpty() && m_streamToggle->isChecked())
		m_streamToggle->setChecked(false); // the next broadcast's key starts folded away
	m_streamCard->setVisible(!s.key.isEmpty());
	if (m_server->text() != s.server)
		m_server->setText(s.server);
	if (m_key->text() != s.key)
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

	// Elide now, and once more after the layout for a freshly shown page has run.
	updateElidedTexts();
	m_elideTimer.start();
	m_rendering = false;
}

void KaiboDock::renderBanner()
{
	const auto &s = m_controller->state();
	const Phase phase = m_controller->phase();

	// Describe the banner first, then touch the widget only when the description
	// changed: render() runs on every controller change (each keystroke), and
	// re-wiring the two banner signals there was pure churn.
	Banner::Tone tone = Banner::Tone::Info;
	QString icon = QStringLiteral("info"), text, primary, secondary;
	int action = 0;
	if (phase == Phase::Dropped) {
		tone = Banner::Tone::Error;
		icon = QStringLiteral("signal");
		text = T("Banner.Dropped");
		primary = T("Banner.Restream");
		secondary = T("Banner.CloseRoom");
		action = 1;
	} else if (phase == Phase::RoomOnly && s.busy == Busy::None) {
		tone = Banner::Tone::Warn;
		icon = QStringLiteral("alert");
		text = T("Banner.RoomOnly");
		primary = T("Banner.StartStream");
		secondary = T("Banner.CloseRoom");
		action = 2;
	} else if (phase == Phase::StreamOnly) {
		tone = Banner::Tone::Warn;
		icon = QStringLiteral("alert");
		text = T("Banner.StreamOnly");
		primary = T("Banner.OpenRoom");
		secondary = T("Banner.StopStream");
		action = 3;
	} else if (s.offline) {
		tone = Banner::Tone::Error;
		icon = QStringLiteral("alert");
		text = T("Msg.Offline");
		primary = T("Loading.Retry");
		action = 4;
	} else if (s.profile.mid > 0 && s.roomId <= 0) {
		tone = Banner::Tone::Info;
		text = T("Banner.NoRoom");
		primary = T("Banner.OpenRoomCenter");
		action = 5;
	}
	if (text.isEmpty()) {
		if (!m_bannerKey.isEmpty()) {
			m_bannerKey.clear();
			m_banner->hide();
		}
		return;
	}
	const QString key = QString::number(action) + QLatin1Char('|') + text + QLatin1Char('|') + primary +
			    QLatin1Char('|') + secondary;
	if (key == m_bannerKey)
		return;
	m_bannerKey = key;
	m_banner->present(tone, icon, text, primary, secondary);
	disconnect(m_banner, &Banner::primaryClicked, nullptr, nullptr);
	disconnect(m_banner, &Banner::secondaryClicked, nullptr, nullptr);
	switch (action) {
	case 1: // dropped: stream again / close the room
	case 2: // room open but OBS not streaming: start streaming / close the room
		connect(m_banner, &Banner::primaryClicked, this, [this] { m_controller->startObsStream(); });
		connect(m_banner, &Banner::secondaryClicked, this, [this] { m_controller->closeRoom(); });
		break;
	case 3: // OBS streaming but room closed: open the room / stop streaming
		connect(m_banner, &Banner::primaryClicked, this, [this] { m_controller->goLive(); });
		connect(m_banner, &Banner::secondaryClicked, this, [this] { m_controller->stopObsStream(); });
		break;
	case 4:
		connect(m_banner, &Banner::primaryClicked, this, [this] { m_controller->refresh(); });
		break;
	case 5:
		connect(m_banner, &Banner::primaryClicked, this,
			[] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kLiveCenterUrl))); });
		break;
	default:
		break;
	}
}

// Renders the face-verification card: the code, what to do with it, and whether the
// controller is already retrying in the background. One card per go-live attempt.
void KaiboDock::renderFaceAuth()
{
	const auto &s = m_controller->state();
	const bool visible = s.auth == Auth::SignedIn && s.face != FaceAuth::None;
	m_faceCard->setVisible(visible);
	if (!visible)
		return;
	const bool retrying = s.face == FaceAuth::Retrying;
	m_faceQr->setPayload(s.faceAuthUrl);
	m_faceQr->setOverlay(QrView::Overlay::None);
	m_faceHeading->setText(retrying ? T("Face.HeadingRetry") : T("Face.Heading"));
	m_faceHint->setText(retrying ? T("Face.RetryAttempt").arg(s.faceAuthAttempt).arg(kFaceAuthMaxRetries)
				     : T("Face.Hint"));
	m_faceConfirm->setEnabled(!retrying);
	m_faceCancel->setEnabled(!retrying);
}

void KaiboDock::renderGoLive()
{
	const auto &s = m_controller->state();
	const Phase phase = m_controller->phase();
	const bool roomLive = s.room.live;
	QString state = QStringLiteral("ready"), text, hint, icon;
	bool enabled = true;
	bool showHint = true; // the usual case repeats the button, so it stays in the tooltip

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
		showHint = false;
		icon = QStringLiteral("stop");
	} else if (s.face == FaceAuth::Required) {
		text = T("Face.WaitingButton");
		hint = T("Face.WaitingHint");
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
		showHint = phase == Phase::StreamOnly || !s.options.autoStartStream;
		icon = QStringLiteral("play");
	}
	// A busy controller or a pending verification means a click cannot do anything:
	// keep the button visibly (and actually) disabled instead of swallowing clicks. A
	// live room always offers "End", even if a stale verification state is still around.
	enabled = enabled && s.busy == Busy::None && (s.face == FaceAuth::None || roomLive);
	if (m_goLive->isEnabled() != enabled)
		m_goLive->setEnabled(enabled);
	Theme::setProp(m_goLive, "kbState", state);
	if (m_goLive->text() != text)
		m_goLive->setText(text);
	// Stable accessible name = the action; the state/hint is the description, so a
	// screen reader hears "Go live, button, disabled — Choose an area first".
	const QString actionName = roomLive ? T("Button.End") : T("Button.Start");
	if (m_goLive->accessibleName() != actionName)
		m_goLive->setAccessibleName(actionName);
	const QString description = (text != actionName && !text.isEmpty())
					    ? (hint.isEmpty() ? text : text + QStringLiteral(" — ") + hint)
					    : hint;
	if (m_goLive->accessibleDescription() != description)
		m_goLive->setAccessibleDescription(description);
	// The icon is a tinted SVG raster; keep it out of the per-keystroke render path.
	const QString iconKey = icon + QLatin1Char('|') + state;
	if (iconKey != m_goLiveIconKey) {
		m_goLiveIconKey = iconKey;
		m_goLive->setIcon(icon.isEmpty()
					  ? QIcon()
					  : QIcon(theme().pixmap(icon,
								 theme().color(state == QStringLiteral("end")
										       ? QStringLiteral("danger")
										       : QStringLiteral("on-brand")),
								 20, devicePixelRatioF())));
	}
	if (m_goLive->toolTip() != hint)
		m_goLive->setToolTip(hint);
	m_goLiveHint->setText(hint);
	m_goLiveHint->setVisible(showHint && !hint.isEmpty());
	m_clockRow->setVisible(roomLive && s.liveSince > 0);
	const bool attention = phase == Phase::Dropped || phase == Phase::RoomOnly || phase == Phase::Reconnecting ||
			       phase == Phase::StreamOnly;
	Theme::setProp(m_actionBar, "kbTally",
		       attention              ? QStringLiteral("warn")
		       : phase == Phase::Live ? QStringLiteral("live")
					      : QStringLiteral("idle"));
	if (roomLive && s.liveSince > 0 && isVisible()) {
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

// Elides the labels that may be wider than the dock. The full values stay in the
// tooltips and accessible names; this only affects what is painted.
void KaiboDock::updateElidedTexts()
{
	auto apply = [](QLabel *label, const QString &full, Qt::TextElideMode mode) {
		const int width = label->width() - 2;
		const QString shown = width > 0 ? label->fontMetrics().elidedText(full, mode, width) : full;
		if (label->text() != shown)
			label->setText(shown);
	};
	apply(m_name, m_nameFull, Qt::ElideRight);
	apply(m_roomLabel, m_roomLabelFull, Qt::ElideRight);
	apply(m_dataPath, m_dataPathFull, Qt::ElideMiddle);
	apply(m_areaText, m_areaFull, Qt::ElideRight);
}

// OBS 33 added a "full-height docks" toggle (Docks menu, QAction objectName
// "sideDocks", config [BasicWindow] SideDocks). With it off, the bottom dock area
// owns the bottom-right corner, so this panel can never span the right column.
// Say so once, politely, and let the person dismiss it for good.
void KaiboDock::updateLayoutHint()
{
	if (!m_hintWrap)
		return; // a changeEvent can arrive while the constructor is still building
	loadLayoutHintDismissed();
	QAction *action = sideDocksAction();
	if (!action && m_hintRetries < 5) {
		// OBS 33 builds its Docks menu after plugins load on a cold start; retry
		// a few times instead of giving up on the first show.
		++m_hintRetries;
		QTimer::singleShot(1500, this, &KaiboDock::updateLayoutHint);
		return;
	}
	const bool show = !m_hintDismissed && action && !action->isChecked();
	if (!show) {
		m_hintWrap->hide();
		return;
	}
	if (m_hintBanner->isHidden())
		m_hintBanner->present(Banner::Tone::Info, QStringLiteral("info"), T("Hint.FullHeight"), {},
				      T("Common.Dismiss"));
	if (!m_hintWrap->isVisible())
		m_hintWrap->show();
}

void KaiboDock::loadLayoutHintDismissed()
{
	if (m_hintDismissLoaded)
		return;
	m_hintDismissLoaded = true;
	const QString directory = m_controller->storageDirectory();
	if (directory.isEmpty())
		return;
	QSettings settings(QDir(directory).filePath(QStringLiteral("ui-hints.ini")), QSettings::IniFormat);
	m_hintDismissed = settings.value(QStringLiteral("fullHeightHintDismissed"), false).toBool();
}

void KaiboDock::dismissLayoutHint()
{
	m_hintDismissed = true;
	m_hintWrap->hide();
	const QString directory = m_controller->storageDirectory();
	if (directory.isEmpty())
		return;
	QSettings settings(QDir(directory).filePath(QStringLiteral("ui-hints.ini")), QSettings::IniFormat);
	settings.setValue(QStringLiteral("fullHeightHintDismissed"), true);
	settings.sync();
}

QAction *KaiboDock::sideDocksAction()
{
	if (m_sideDocksAction)
		return m_sideDocksAction;
	QMainWindow *main = qobject_cast<QMainWindow *>(window());
	if (!main) {
		// A floating dock's window() is the QDockWidget; fall back to the app's main
		// window so the hint also appears while the panel is undocked.
		for (QWidget *widget : QApplication::topLevelWidgets()) {
			main = qobject_cast<QMainWindow *>(widget);
			if (main)
				break;
		}
	}
	if (!main)
		return nullptr;
	m_sideDocksAction = main->findChild<QAction *>(QStringLiteral("sideDocks"));
	if (m_sideDocksAction)
		connect(m_sideDocksAction, &QAction::toggled, this, &KaiboDock::updateLayoutHint);
	return m_sideDocksAction;
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
		const int side = b->iconSize().isValid() ? b->iconSize().width() : 18;
		b->setIcon(theme().icon(icon, token.isEmpty() ? QStringLiteral("ink-muted") : token, side));
	}
	for (QLabel *l : findChildren<QLabel *>()) {
		const QString icon = l->property("kbIcon").toString();
		if (icon.isEmpty())
			continue;
		const QString token = l->property("kbIconToken").toString();
		l->setPixmap(theme().pixmap(icon, theme().color(token), l->width(), devicePixelRatioF()));
	}
	m_banner->refreshIcon();
	m_hintBanner->refreshIcon();
	m_loginBanner->refreshIcon();
	m_legacyBanner->refreshIcon();
	m_toast->refreshIcon();
	m_picker->refreshTheme();
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
	// The dock can be re-parented when OBS restores the saved layout or the panel is
	// floated; the main-window dock action may only become reachable then.
	if (event->type() == QEvent::ParentChange)
		updateLayoutHint();
}

void KaiboDock::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	// Elide once now and once after the layout settles; the single-shot timer
	// coalesces the bursts produced while the dock separator is dragged.
	updateElidedTexts();
	m_elideTimer.start();
}

void KaiboDock::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	m_controller->setDockVisible(true);
	if (theme().followPalette(QApplication::palette()))
		applyTheme();
	m_pill->setActive(true);
	render();
	renderClock();
	updateLayoutHint();
	renderGoLive(); // reschedules the live clock
	// Do not hit the network every time an OBS dock is re-shown.
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (m_controller->state().auth == Auth::SignedIn && now - m_lastRefreshMs > 5000) {
		m_lastRefreshMs = now;
		m_controller->refresh();
	}
}

void KaiboDock::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);
	m_controller->setDockVisible(false);
	// Nothing ticks, blinks or re-renders while the dock is hidden.
	m_renderTimer.stop();
	m_elideTimer.stop();
	m_clockTimer.stop();
	m_pill->setActive(false);
	if (m_controller->state().auth == Auth::SignedOut)
		m_controller->stopQrLogin(); // no polling while nobody can see the code
	if (m_keyReveal->isChecked())
		m_keyReveal->setChecked(false); // re-mask the stream key
}

// ------------------------------------------------------------------ actions

void KaiboDock::requestGoLive()
{
	// Shared by the button, the OBS hotkey and Ctrl+Enter. It never ends a
	// broadcast and ignores the shortcut while another action is pending.
	if (m_settingsOpen)
		return;
	const auto &s = m_controller->state();
	if (s.auth != Auth::SignedIn || s.busy != Busy::None || s.face != FaceAuth::None)
		return;
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
		QPushButton *cancel = box.addButton(T("Common.Cancel"), QMessageBox::RejectRole);
		box.setDefaultButton(cancel); // Enter must not end the broadcast
		box.setEscapeButton(cancel);
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

// Anchors a popup menu to the button that opened it; when the button is activated
// from the keyboard QCursor::pos() may be anywhere on screen.
void KaiboDock::execMenuAt(QMenu &menu, QWidget *anchor)
{
	if (!anchor)
		return;
	menu.ensurePolished();
	const QSize hint = menu.sizeHint();
	QPoint pos = anchor->mapToGlobal(QPoint(0, anchor->height() + 4));
	if (const QScreen *screen = anchor->screen()) {
		const QRect area = screen->availableGeometry();
		if (pos.y() + hint.height() > area.bottom())
			pos.setY(anchor->mapToGlobal(QPoint(0, -hint.height() - 4)).y());
		pos.setX(qBound(area.left(), pos.x(), area.right() - hint.width()));
	}
	menu.exec(pos);
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
	m_titleHistoryButton->setDown(false);
	execMenuAt(menu, m_titleHistoryButton);
}

void KaiboDock::showAccountMenu()
{
	const auto &s = m_controller->state();
	QMenu menu(this);
	connect(menu.addAction(theme().icon(QStringLiteral("external"), QStringLiteral("ink-muted"), 16),
			       T("Menu.OpenCenter")),
		&QAction::triggered, this,
		[] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kLiveCenterUrl))); });
	if (s.roomId > 0)
		connect(menu.addAction(theme().icon(QStringLiteral("copy"), QStringLiteral("ink-muted"), 16),
				       T("Menu.CopyRoomLink")),
			&QAction::triggered, this,
			[this, id = s.roomId] { copyText(roomLink(id), T("Menu.CopyRoomLink")); });
	connect(menu.addAction(theme().icon(QStringLiteral("refresh"), QStringLiteral("ink-muted"), 16),
			       T("Menu.Refresh")),
		&QAction::triggered, m_controller, &LiveController::refresh);
	menu.addSeparator();
	connect(menu.addAction(theme().icon(QStringLiteral("settings"), QStringLiteral("ink-muted"), 16),
			       T("Settings.Title")),
		&QAction::triggered, this, [this] { setSettingsOpen(true); });
	m_moreButton->setDown(false);
	execMenuAt(menu, m_moreButton);
}

void KaiboDock::confirmDeleteData()
{
	const QString folder = m_controller->storageDirectory();
	const bool live = m_controller->state().room.live;
	QMessageBox box(QMessageBox::Warning, T("Settings.DeleteData"),
			(live ? T("Confirm.DeleteDataLive") : T("Confirm.DeleteData")).arg(folder),
			QMessageBox::NoButton, this);
	QPushButton *wipe = box.addButton(T("Settings.DeleteData"), QMessageBox::DestructiveRole);
	QPushButton *cancel = box.addButton(T("Common.Cancel"), QMessageBox::RejectRole);
	box.setDefaultButton(cancel); // the destructive choice must never be the default
	box.setEscapeButton(cancel);
	box.exec();
	if (box.clickedButton() != wipe)
		return;
	QString error;
	if (m_controller->deleteAllData(error))
		m_toast->showMessage(ToastKind::Ok, T("Msg.DataDeleted"), {});
	else
		m_toast->showMessage(ToastKind::Error, T("Msg.DataDeleteFailed"), error);
	setSettingsOpen(false);
}

void KaiboDock::copyText(const QString &text, const QString &what)
{
	if (text.isEmpty())
		return;
	QApplication::clipboard()->setText(text);
	// The caption line says what was copied — stream key and server differ a lot
	// in consequence.
	m_toast->showMessage(ToastKind::Ok, T("Msg.Copied"), what);
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
