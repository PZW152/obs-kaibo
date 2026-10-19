// SPDX-License-Identifier: GPL-2.0-only
#include "ui/widgets.hpp"
#include "app/i18n.hpp"
#include "ui/theme.hpp"
#include "vendor/qrcodegen/qrcodegen.hpp"

#include <QAccessible>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace kaibo::ui {
namespace {

Theme &theme()
{
	return Theme::instance();
}

} // namespace

// ------------------------------------------------------------------ helpers

QLabel *label(const QString &text, const char *role, QWidget *parent)
{
	auto *l = new QLabel(text, parent);
	if (role)
		l->setProperty("kbText", QString::fromLatin1(role));
	l->setWordWrap(role && (qstrcmp(role, "caption") == 0 || qstrcmp(role, "faint") == 0));
	return l;
}

QPushButton *button(const QString &text, const char *variant, QWidget *parent)
{
	auto *b = new QPushButton(text, parent);
	if (variant)
		b->setProperty("kbVariant", QString::fromLatin1(variant));
	b->setCursor(Qt::PointingHandCursor);
	return b;
}

QPushButton *iconButton(const QString &icon, const QString &tooltip, QWidget *parent)
{
	auto *b = new QPushButton(parent);
	b->setProperty("kbVariant", QStringLiteral("icon"));
	b->setProperty("kbIcon", icon);
	b->setIcon(theme().icon(icon));
	b->setIconSize(QSize(16, 16));
	b->setToolTip(tooltip);
	b->setAccessibleName(tooltip);
	b->setCursor(Qt::PointingHandCursor);
	return b;
}

QFrame *card(QWidget *parent)
{
	auto *f = new QFrame(parent);
	f->setProperty("kbCard", true);
	return f;
}

QFrame *divider(QWidget *parent)
{
	auto *f = new QFrame(parent);
	f->setProperty("kbDivider", true);
	f->setFixedHeight(1);
	return f;
}

// ------------------------------------------------------------------ StatusPill

StatusPill::StatusPill(QWidget *parent) : QWidget(parent)
{
	m_blink.setInterval(600);
	connect(&m_blink, &QTimer::timeout, this, [this] {
		m_blinkOn = !m_blinkOn;
		update();
	});
	setAccessibleName(QStringLiteral("status"));
}

void StatusPill::set(Tone tone, const QString &text)
{
	if (tone == m_tone && text == m_text)
		return;
	m_tone = tone;
	m_text = text;
	m_blinkOn = true;
	if (tone == Tone::Busy)
		m_blink.start();
	else
		m_blink.stop();
	setAccessibleDescription(text);
	updateGeometry();
	update();
}

QSize StatusPill::sizeHint() const
{
	QFont f = font();
	f.setPixelSize(11);
	f.setWeight(QFont::DemiBold);
	return {QFontMetrics(f).horizontalAdvance(m_text) + 7 + 7 + 6 + 8 + 2, 22};
}

void StatusPill::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	QColor bg = theme().color("surface-raised"), fg = theme().color("ink-muted"), dot = fg;
	QColor border = theme().color("line");
	switch (m_tone) {
	case Tone::Neutral:
		break;
	case Tone::Busy:
		fg = dot = theme().color("info");
		break;
	case Tone::Live:
		bg = theme().color("brand-soft");
		fg = theme().color("brand-ink");
		dot = theme().color("brand");
		border = bg;
		break;
	case Tone::Warn:
		bg = theme().color("warning-soft");
		fg = dot = theme().color("warning");
		border = bg;
		break;
	case Tone::Ok:
		bg = theme().color("success-soft");
		fg = dot = theme().color("success");
		border = bg;
		break;
	}
	const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	p.setPen(QPen(border, 1));
	p.setBrush(bg);
	p.drawRoundedRect(r, r.height() / 2, r.height() / 2);

	if (m_tone == Tone::Live) {
		QColor halo = dot;
		halo.setAlphaF(0.25);
		p.setPen(Qt::NoPen);
		p.setBrush(halo);
		p.drawEllipse(QPointF(10.5, r.center().y()), 6.5, 6.5);
	}
	if (m_blinkOn) {
		p.setPen(Qt::NoPen);
		p.setBrush(dot);
		p.drawEllipse(QPointF(10.5, r.center().y()), 3.5, 3.5);
	}
	QFont f = font();
	f.setPixelSize(11);
	f.setWeight(QFont::DemiBold);
	p.setFont(f);
	p.setPen(fg);
	p.drawText(QRectF(20, 0, width() - 26, height()), Qt::AlignVCenter | Qt::AlignLeft, m_text);
}

// ------------------------------------------------------------------ Avatar

Avatar::Avatar(QWidget *parent) : QWidget(parent)
{
	setFixedSize(40, 40);
}

void Avatar::setImage(const QImage &image)
{
	m_image = image;
	update();
}

void Avatar::setName(const QString &name)
{
	m_initial = name.isEmpty() ? QString() : name.left(1).toUpper();
	setAccessibleName(name);
	update();
}

void Avatar::setLive(bool live)
{
	if (live != m_live) {
		m_live = live;
		update();
	}
}

void Avatar::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	const QRectF r(1.5, 1.5, 37, 37);
	p.setPen(QPen(theme().color("line"), 1));
	p.setBrush(theme().color("brand-soft"));
	p.drawEllipse(r);
	if (!m_image.isNull()) {
		QPainterPath clip;
		clip.addEllipse(r.adjusted(1, 1, -1, -1));
		p.save();
		p.setClipPath(clip);
		p.drawImage(r.adjusted(1, 1, -1, -1), m_image);
		p.restore();
	} else {
		QFont f = font();
		f.setPixelSize(15);
		f.setWeight(QFont::DemiBold);
		p.setFont(f);
		p.setPen(theme().color("brand-ink"));
		p.drawText(r, Qt::AlignCenter, m_initial);
	}
	if (m_live) {
		p.setPen(QPen(theme().color("surface"), 2));
		p.setBrush(theme().color("brand"));
		p.drawEllipse(QRectF(28, 28, 11, 11));
	}
}

// ------------------------------------------------------------------ QrView

QrView::QrView(QWidget *parent) : QWidget(parent)
{
	setFixedSize(184, 184);
	setCursor(Qt::PointingHandCursor);
	setAccessibleName(T("Login.QrAccessible"));
}

void QrView::setPayload(const QString &text)
{
	m_modules.clear();
	m_size = 0;
	if (!text.isEmpty()) {
		try {
			const auto qr = qrcodegen::QrCode::encodeText(text.toUtf8().constData(),
								      qrcodegen::QrCode::Ecc::MEDIUM);
			m_size = qr.getSize();
			m_modules.resize(static_cast<size_t>(m_size * m_size));
			for (int y = 0; y < m_size; ++y)
				for (int x = 0; x < m_size; ++x)
					m_modules[static_cast<size_t>(y * m_size + x)] = qr.getModule(x, y);
		} catch (...) {
			m_size = 0;
			m_modules.clear();
		}
	}
	update();
}

void QrView::setOverlay(Overlay overlay, const QString &text)
{
	m_overlay = overlay;
	m_overlayText = text;
	update();
}

void QrView::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton && (m_overlay == Overlay::Expired || m_overlay == Overlay::Failed))
		emit clicked();
	QWidget::mouseReleaseEvent(event);
}

void QrView::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal radius = theme().length("radius-lg", 14);
	p.setPen(QPen(theme().color("line"), 1));
	p.setBrush(theme().color("qr-ground"));
	p.drawRoundedRect(frame, radius, radius);

	const bool faded = m_overlay == Overlay::Expired || m_overlay == Overlay::Failed;
	if (m_size > 0) {
		const qreal inner = width() - 24.0;
		const qreal cell = std::floor(inner / m_size * 4) / 4; // crisp-ish at fractional DPR
		const qreal origin = (width() - cell * m_size) / 2;
		QColor ink = theme().color("qr-ink");
		if (faded)
			ink.setAlphaF(0.2);
		p.setRenderHint(QPainter::Antialiasing, false);
		p.setPen(Qt::NoPen);
		p.setBrush(ink);
		for (int y = 0; y < m_size; ++y)
			for (int x = 0; x < m_size; ++x)
				if (m_modules[static_cast<size_t>(y * m_size + x)])
					p.drawRect(QRectF(origin + x * cell, origin + y * cell, cell, cell));
		p.setRenderHint(QPainter::Antialiasing, true);
	}

	if (m_overlay == Overlay::None)
		return;

	QFont f = font();
	f.setPixelSize(13);
	f.setWeight(QFont::DemiBold);
	p.setFont(f);
	if (m_overlay == Overlay::Scanned) {
		p.setPen(Qt::NoPen);
		p.setBrush(theme().color("scrim"));
		p.drawRoundedRect(frame, radius, radius);
		p.setBrush(theme().color("success"));
		const QPointF c(width() / 2.0, height() / 2.0 - 14);
		p.drawEllipse(c, 20, 20);
		p.drawPixmap(QRectF(c.x() - 11, c.y() - 11, 22, 22).toRect(),
			     theme().pixmap("check", QColor("#0e0f13"), 22, devicePixelRatioF()));
		p.setPen(Qt::white);
		p.drawText(QRectF(8, c.y() + 26, width() - 16, 40), Qt::AlignHCenter | Qt::TextWordWrap, m_overlayText);
		return;
	}
	if (m_overlay == Overlay::Loading) {
		p.setPen(theme().color("ink-faint"));
		p.drawText(rect(), Qt::AlignCenter, m_overlayText);
		return;
	}
	// Expired / Failed: a refresh call to action on the faded code.
	const QRectF pill(width() / 2.0 - 66, height() / 2.0 - 17, 132, 34);
	p.setPen(Qt::NoPen);
	p.setBrush(theme().color("brand"));
	p.drawRoundedRect(pill, 10, 10);
	p.drawPixmap(QRectF(pill.left() + 12, pill.center().y() - 8, 16, 16).toRect(),
		     theme().pixmap("refresh", theme().color("on-brand"), 16, devicePixelRatioF()));
	p.setPen(theme().color("on-brand"));
	p.drawText(pill.adjusted(32, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, m_overlayText);
}

// ------------------------------------------------------------------ ToggleSwitch

ToggleSwitch::ToggleSwitch(QWidget *parent) : QPushButton(parent)
{
	setCheckable(true);
	setFixedSize(34, 20);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::StrongFocus);
	setStyleSheet(QStringLiteral("QPushButton{border:none;background:transparent;min-height:0;padding:0;}"));
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	if (isChecked()) {
		p.setPen(Qt::NoPen);
		p.setBrush(theme().color("brand"));
	} else {
		p.setPen(QPen(theme().color("line-strong"), 1));
		p.setBrush(theme().color("surface-raised"));
	}
	p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
	p.setPen(Qt::NoPen);
	p.setBrush(isChecked() ? theme().color("on-brand") : theme().color("ink-muted"));
	const qreal x = isChecked() ? r.right() - 16 : r.left() + 2;
	p.drawEllipse(QRectF(x, r.top() + 2.5, 14, 14));
	if (hasFocus()) {
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(theme().color("focus"), 2));
		p.drawRoundedRect(r.adjusted(-0.5, -0.5, 0.5, 0.5), r.height() / 2, r.height() / 2);
	}
}

// ------------------------------------------------------------------ Banner

Banner::Banner(QWidget *parent) : QFrame(parent)
{
	auto *row = new QHBoxLayout(this);
	row->setContentsMargins(12, 10, 12, 10);
	row->setSpacing(10);
	m_icon = new QLabel(this);
	m_icon->setFixedSize(16, 16);
	row->addWidget(m_icon, 0, Qt::AlignTop);

	auto *col = new QVBoxLayout();
	col->setSpacing(8);
	m_text = new QLabel(this);
	m_text->setWordWrap(true);
	m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
	col->addWidget(m_text);
	auto *actions = new QHBoxLayout();
	actions->setSpacing(8);
	m_primary = button({}, "primary", this);
	m_primary->setProperty("kbSize", QStringLiteral("sm"));
	m_secondary = button({}, "ghost", this);
	m_secondary->setProperty("kbSize", QStringLiteral("sm"));
	actions->addWidget(m_primary);
	actions->addWidget(m_secondary);
	actions->addStretch();
	col->addLayout(actions);
	row->addLayout(col, 1);

	connect(m_primary, &QPushButton::clicked, this, &Banner::primaryClicked);
	connect(m_secondary, &QPushButton::clicked, this, &Banner::secondaryClicked);
	setAccessibleName(QStringLiteral("banner"));
	hide();
}

void Banner::present(Tone tone, const QString &icon, const QString &text, const QString &primary,
		     const QString &secondary)
{
	m_tone = tone;
	m_iconName = icon;
	Theme::setProp(this, "kbBanner",
		       tone == Tone::Warn    ? QStringLiteral("warn")
		       : tone == Tone::Error ? QStringLiteral("error")
					     : QStringLiteral("info"));
	m_text->setText(text);
	m_primary->setText(primary);
	m_primary->setVisible(!primary.isEmpty());
	m_secondary->setText(secondary);
	m_secondary->setVisible(!secondary.isEmpty());
	setAccessibleDescription(text);
	refreshIcon();
	QFrame::show();
}

void Banner::refreshIcon()
{
	const char *token = m_tone == Tone::Warn ? "warning" : m_tone == Tone::Error ? "danger" : "info";
	m_icon->setPixmap(theme().pixmap(m_iconName, theme().color(token), 16, devicePixelRatioF()));
}

// ------------------------------------------------------------------ Toast

Toast::Toast(QWidget *parent) : QFrame(parent)
{
	setObjectName(QStringLiteral("KbToast"));
	auto *row = new QHBoxLayout(this);
	row->setContentsMargins(12, 10, 12, 10);
	row->setSpacing(10);
	m_icon = new QLabel(this);
	m_icon->setFixedSize(16, 16);
	row->addWidget(m_icon, 0, Qt::AlignTop);
	auto *col = new QVBoxLayout();
	col->setSpacing(2);
	m_text = new QLabel(this);
	m_text->setWordWrap(true);
	m_detail = label({}, "caption", this);
	col->addWidget(m_text);
	col->addWidget(m_detail);
	row->addLayout(col, 1);
	m_hide.setSingleShot(true);
	connect(&m_hide, &QTimer::timeout, this, &QWidget::hide);
	hide();
}

void Toast::showMessage(int kind, const QString &text, const QString &detail)
{
	// kind: 0 ok, 1 info, 2 error (LiveController::ToastKind)
	const char *icon = kind == 0 ? "check" : kind == 2 ? "alert" : "info";
	const char *token = kind == 0 ? "success" : kind == 2 ? "danger" : "info";
	m_icon->setPixmap(theme().pixmap(icon, theme().color(token), 16, devicePixelRatioF()));
	m_text->setText(text);
	m_detail->setText(detail);
	m_detail->setVisible(!detail.isEmpty());
	reposition();
	QFrame::show();
	raise();
	m_hide.start(kind == 2 ? 8000 : 4000);
	QAccessibleEvent event(this, QAccessible::Alert);
	QAccessible::updateAccessibility(&event);
}

void Toast::mousePressEvent(QMouseEvent *event)
{
	hide(); // click to dismiss
	QFrame::mousePressEvent(event);
}

void Toast::reposition()
{
	if (!parentWidget())
		return;
	const int margin = 12;
	const int w = parentWidget()->width() - margin * 2;
	setFixedWidth(std::max(160, w));
	adjustSize();
	move(margin, parentWidget()->height() - height() - margin);
}

// ------------------------------------------------------------------ FlowLayout

FlowLayout::FlowLayout(QWidget *parent, int spacing) : QLayout(parent), m_gap(spacing)
{
	setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
	for (QLayoutItem *item : m_items)
		delete item;
}

void FlowLayout::addItem(QLayoutItem *item)
{
	m_items.push_back(item);
}

QLayoutItem *FlowLayout::itemAt(int index) const
{
	return index >= 0 && index < count() ? m_items[static_cast<size_t>(index)] : nullptr;
}

QLayoutItem *FlowLayout::takeAt(int index)
{
	if (index < 0 || index >= count())
		return nullptr;
	QLayoutItem *item = m_items[static_cast<size_t>(index)];
	m_items.erase(m_items.begin() + index);
	return item;
}

QSize FlowLayout::minimumSize() const
{
	QSize size;
	for (QLayoutItem *item : m_items)
		size = size.expandedTo(item->minimumSize());
	return size;
}

void FlowLayout::setGeometry(const QRect &rect)
{
	QLayout::setGeometry(rect);
	arrange(rect, false);
}

int FlowLayout::arrange(const QRect &rect, bool dryRun) const
{
	int x = rect.x(), y = rect.y(), lineHeight = 0;
	for (QLayoutItem *item : m_items) {
		const QSize hint = item->sizeHint();
		if (x + hint.width() > rect.right() + 1 && lineHeight > 0) {
			x = rect.x();
			y += lineHeight + m_gap;
			lineHeight = 0;
		}
		if (!dryRun)
			item->setGeometry(QRect(QPoint(x, y), hint));
		x += hint.width() + m_gap;
		lineHeight = std::max(lineHeight, hint.height());
	}
	return y + lineHeight - rect.y();
}

} // namespace kaibo::ui
