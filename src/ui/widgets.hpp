// SPDX-License-Identifier: GPL-2.0-only
// Small painted widgets that follow the design system's components.
#pragma once

#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QPixmap>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <vector>

class QBoxLayout;
class QHBoxLayout;
class QKeyEvent;
class QVBoxLayout;

namespace kaibo::ui {

// Toast severities. Kept separate from LiveController::ToastKind so widgets do not
// need to know about the controller; the dock maps one to the other.
enum class ToastKind { Ok, Info, Error };

// StatusPill: dot + word. Colour never carries the state alone.
class StatusPill : public QWidget {
	Q_OBJECT
public:
	enum class Tone { Neutral, Busy, Live, Warn, Ok };
	explicit StatusPill(QWidget *parent = nullptr);
	void set(Tone tone, const QString &text);
	// Stops the busy blink while the dock is hidden; set() restarts it if needed.
	void setActive(bool active);
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *) override;

private:
	Tone m_tone = Tone::Neutral;
	QString m_text;
	QTimer m_blink;
	bool m_blinkOn = true;
	bool m_active = true;
};

// Round avatar with initials fallback and an optional live lamp.
class Avatar : public QWidget {
	Q_OBJECT
public:
	explicit Avatar(QWidget *parent = nullptr);
	void setImage(const QImage &image);
	void setName(const QString &name);
	void setLive(bool live);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *) override;

private:
	void rebuildScaled();

	QImage m_image;
	QPixmap m_scaled;
	qint64 m_imageKey = 0;
	qreal m_scaledDpr = 0;
	QString m_name;
	QString m_initial;
	bool m_live = false;
};

// QR code painted crisply at any DPI, with waiting / scanned / expired overlays.
class QrView : public QWidget {
	Q_OBJECT
public:
	enum class Overlay { None, Loading, Scanned, Expired, Failed };
	explicit QrView(QWidget *parent = nullptr);
	void setPayload(const QString &text);
	void setOverlay(Overlay overlay, const QString &text = {});
	QSize sizeHint() const override;

signals:
	void clicked();

protected:
	void paintEvent(QPaintEvent *) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

private:
	std::vector<bool> m_modules;
	int m_size = 0;
	Overlay m_overlay = Overlay::Loading;
	QString m_overlayText;
	QString m_payload;
};

// Switch (role: check box) for settings rows.
class ToggleSwitch : public QPushButton {
	Q_OBJECT
public:
	explicit ToggleSwitch(QWidget *parent = nullptr);
	QSize sizeHint() const override { return {34, 20}; }

protected:
	void paintEvent(QPaintEvent *) override;
};

// Inline banner with icon, text and up to two actions. The action row stacks
// vertically when the dock is narrow so neither button label is ever clipped.
class Banner : public QFrame {
	Q_OBJECT
public:
	enum class Tone { Warn, Error, Info };
	explicit Banner(QWidget *parent = nullptr);
	void present(Tone tone, const QString &icon, const QString &text, const QString &primary,
		     const QString &secondary = {});
	void refreshIcon();

signals:
	void primaryClicked();
	void secondaryClicked();

protected:
	void resizeEvent(QResizeEvent *event) override;

private:
	void updateActionDirection();

	Tone m_tone = Tone::Info;
	QString m_iconName;
	QLabel *m_icon;
	QLabel *m_text;
	QBoxLayout *m_actions;
	QPushButton *m_primary;
	QPushButton *m_secondary;
};

// Transient message row. It is a normal row under the scrolling content (not an
// overlay), so it never covers a control and never swallows a click meant for one.
class Toast : public QFrame {
	Q_OBJECT
public:
	explicit Toast(QWidget *parent);
	void showMessage(ToastKind kind, const QString &text, const QString &detail = {});
	void refreshIcon();

signals:
	void visibilityChanged();

protected:
	void mousePressEvent(QMouseEvent *event) override;
	void hideEvent(QHideEvent *event) override;

private:
	ToastKind m_kind = ToastKind::Info;
	QLabel *m_icon;
	QLabel *m_text;
	QLabel *m_detail;
	QTimer m_hide;
};

// Wrapping row of chips.
class FlowLayout : public QLayout {
public:
	explicit FlowLayout(QWidget *parent, int spacing = 6);
	~FlowLayout() override;
	void addItem(QLayoutItem *item) override;
	int count() const override { return static_cast<int>(m_items.size()); }
	QLayoutItem *itemAt(int index) const override;
	QLayoutItem *takeAt(int index) override;
	Qt::Orientations expandingDirections() const override { return {}; }
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), true); }
	QSize sizeHint() const override { return minimumSize(); }
	QSize minimumSize() const override;
	void setGeometry(const QRect &rect) override;

private:
	int arrange(const QRect &rect, bool dryRun) const;
	std::vector<QLayoutItem *> m_items;
	int m_gap;
};

// Helpers shared by the dock's builders.
QLabel *label(const QString &text, const char *role = nullptr, QWidget *parent = nullptr);
QPushButton *button(const QString &text, const char *variant = nullptr, QWidget *parent = nullptr);
QPushButton *iconButton(const QString &icon, const QString &tooltip, QWidget *parent = nullptr);
QFrame *card(QWidget *parent = nullptr);
QFrame *divider(QWidget *parent = nullptr);

} // namespace kaibo::ui
