// Renders the real KaiboDock in every state to PNG. Built with -fno-access-control.
#include "app/controller.hpp"
#include "ui/dock.hpp"
#include "ui/area_picker.hpp"
#include "ui/widgets.hpp"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QLayout>
#include <QScrollArea>
#include <cstdlib>
#include <functional>
#include <vector>

using namespace kaibo;
using C = LiveController;

struct Scene {
	const char *name;
	int width;
	int height;
	std::function<void(C::State &, ui::KaiboDock &)> setup;
};

static std::vector<AreaGroup> areas()
{
	AreaGroup a{2, "网游", {}};
	a.areas.push_back({86, "英雄联盟", 2, "网游", false});
	a.areas.push_back({329, "无畏契约", 2, "网游", false});
	AreaGroup b{9, "虚拟主播", {}};
	b.areas.push_back({371, "虚拟日常", 9, "虚拟主播", true});
	AreaGroup c{1, "娱乐", {}};
	c.areas.push_back({21, "视频唱见", 1, "娱乐", false});
	c.areas.push_back({145, "聊天电台", 1, "娱乐", false});
	return {a, b, c};
}

static void signedIn(C::State &s)
{
	s.auth = C::Auth::SignedIn;
	s.profile.mid = 12345;
	s.profile.name = "夏日晚风Studio";
	s.roomId = 21452505;
	s.roomLoaded = true;
	s.room.roomId = s.roomId;
	s.room.title = "今晚冲分！英雄联盟峡谷之巅";
	s.room.areaId = 86;
	s.room.areaName = "英雄联盟";
	s.room.parentAreaName = "网游";
	s.room.followers = 12840;
	s.room.online = 1284;
	s.titleDraft = QString::fromUtf8("今晚冲分！英雄联盟峡谷之巅");
	s.areaDraft = 86;
	s.newsLoaded = true;
	s.areas = areas();
	s.recentAreas = {86, 145};
	s.streamTargetIsBilibili = true;
}

static void live(C::State &s)
{
	signedIn(s);
	s.room.live = true;
	s.obsStreaming = true;
	s.liveSince = QDateTime::currentSecsSinceEpoch() - 5076;
	s.server = "rtmp://live-push.bilivideo.com/live-bvc/";
	s.key = "?streamname=live_123456_7890&key=abcdef";
}

int main(int argc, char **argv)
{
	const bool dark = qEnvironmentVariable("KB_THEME", "dark") == "dark";
	const QString out = qEnvironmentVariable("KB_OUT", ".");
	const QStringList only = qEnvironmentVariable("KB_ONLY").split(',', Qt::SkipEmptyParts);
	QApplication app(argc, argv);
	QPalette pal;
	pal.setColor(QPalette::Window, dark ? QColor(0x1d, 0x1f, 0x26) : QColor(0xf0, 0xf0, 0xf0));
	pal.setColor(QPalette::WindowText, dark ? Qt::white : Qt::black);
	app.setPalette(pal);
	QFont font = app.font();
	font.setFamilies({"Noto Sans CJK SC", "Noto Sans"});
	font.setPointSize(9);
	app.setFont(font);
	QDir().mkpath(out);

	const QString qr =
		"https://passport.bilibili.com/h5-app/passport/login/scan?navhide=1&qrcode_key=0123456789abcdef";
	std::vector<Scene> scenes = {
		{"01-login", 320, 0,
		 [&](C::State &s, ui::KaiboDock &) {
			 s.auth = C::Auth::SignedOut;
			 s.qr = C::QrPhase::Waiting;
			 s.qrUrl = qr;
		 }},
		{"02-login-expired", 320, 0,
		 [&](C::State &s, ui::KaiboDock &) {
			 s.auth = C::Auth::SignedOut;
			 s.qr = C::QrPhase::Expired;
			 s.qrUrl = qr;
		 }},
		{"03-ready", 320, 0,
		 [](C::State &s, ui::KaiboDock &) {
			 signedIn(s);
		 }},
		{"04-unsaved-news", 320, 0,
		 [](C::State &s, ui::KaiboDock &d) {
			 signedIn(s);
			 s.titleDraft = QString::fromUtf8("今晚上王者！");
			 s.newsDraft = QString::fromUtf8("每晚八点开播");
			 d.m_newsExpanded = true;
		 }},
		{"05-starting", 320, 0,
		 [](C::State &s, ui::KaiboDock &) {
			 signedIn(s);
			 s.busy = C::Busy::GoingLive;
		 }},
		{"06-live", 320, 0,
		 [](C::State &s, ui::KaiboDock &) {
			 live(s);
		 }},
		{"07-dropped", 320, 0,
		 [](C::State &s, ui::KaiboDock &) {
			 live(s);
			 s.obsStreaming = false;
			 s.dropped = true;
		 }},
		{"08-room-only", 320, 0,
		 [](C::State &s, ui::KaiboDock &) {
			 live(s);
			 s.obsStreaming = false;
			 s.key.clear();
			 s.server.clear();
		 }},
		{"09-face", 320, 0,
		 [&](C::State &s, ui::KaiboDock &) {
			 signedIn(s);
			 s.face = C::FaceAuth::Required;
			 s.faceAuthUrl = qr;
		 }},
		{"10-settings", 320, 0,
		 [](C::State &s, ui::KaiboDock &d) {
			 signedIn(s);
			 d.m_settingsOpen = true;
		 }},
		{"11-no-room", 320, 0,
		 [](C::State &s, ui::KaiboDock &) {
			 s.auth = C::Auth::SignedIn;
			 s.profile.mid = 1;
			 s.profile.name = "新用户";
			 s.newsLoaded = false;
		 }},
		{"12-offline", 320, 0,
		 [](C::State &s, ui::KaiboDock &) {
			 signedIn(s);
			 s.offline = true;
		 }},
		{"13-narrow-live", 280, 0,
		 [](C::State &s, ui::KaiboDock &) {
			 live(s);
			 s.room.title = "一个特别特别特别长的直播标题用来测试";
			 s.titleDraft = QString::fromUtf8("一个特别特别特别长的直播标题用来测试");
			 s.profile.name = "名字非常非常非常非常非常非常长的主播";
		 }},
		{"15-short-live", 320, 420,
		 [](C::State &s, ui::KaiboDock &) {
			 live(s);
		 }},
		{"16-stream-open", 320, 0,
		 [](C::State &s, ui::KaiboDock &d) {
			 live(s);
			 d.m_streamToggle->setChecked(true);
		 }},
		{"14-wide-ready", 420, 0,
		 [](C::State &s, ui::KaiboDock &) {
			 signedIn(s);
		 }},
	};

	std::vector<QImage> sheet;
	for (const Scene &scene : scenes) {
		if (!only.isEmpty() && !only.contains(QString::fromLatin1(scene.name).left(2)))
			continue;
		auto *controller = new C();
		controller->m_settings = Settings();
		auto *dock = new ui::KaiboDock(controller);
		scene.setup(controller->m_state, *dock);
		controller->m_state.preflight = controller->preflight();
		dock->resize(scene.width, 900);
		dock->show();
		dock->render();
		dock->renderQr();
		for (int i = 0; i < 4; ++i)
			app.processEvents();
		dock->updateElidedTexts();
		// Tall enough for the whole page: size to the scroll content.
		QWidget *content = dock->m_scroll->widget();
		const int need = content->layout()->totalHeightForWidth(scene.width) + 60 +
				 (dock->m_actionBar->isVisible() ? dock->m_actionBar->sizeHint().height() : 0);
		dock->resize(scene.width, scene.height > 0 ? scene.height : std::max(420, need));
		if (QWidget *focused = QApplication::focusWidget())
			focused->clearFocus();
		for (int i = 0; i < 4; ++i)
			app.processEvents();
		dock->updateElidedTexts();
		const QImage img = dock->grab().toImage();
		img.save(out + "/" + scene.name + ".png");
		sheet.push_back(img);
		dock->hide();
	}
	// Contact sheets of up to four scenes.
	for (size_t base = 0, n = 0; base < sheet.size(); base += 4, ++n) {
		int w = 12, h = 0;
		for (size_t i = base; i < std::min(base + 4, sheet.size()); ++i) {
			w += sheet[i].width() + 12;
			h = std::max(h, sheet[i].height());
		}
		QImage canvas(w, h + 24, QImage::Format_ARGB32);
		canvas.fill(dark ? QColor(60, 60, 66) : QColor(200, 200, 205));
		QPainter p(&canvas);
		int x = 12;
		for (size_t i = base; i < std::min(base + 4, sheet.size()); ++i) {
			p.drawImage(x, 12, sheet[i]);
			x += sheet[i].width() + 12;
		}
		p.end();
		canvas.save(out + QString("/sheet-%1.png").arg(n));
	}
	fflush(stdout);
	std::_Exit(0);
}
