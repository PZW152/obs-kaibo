// SPDX-License-Identifier: GPL-2.0-only
// Small painted widgets that follow the design system's components.
#pragma once

#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <vector>

class QHBoxLayout;
class QVBoxLayout;

namespace kaibo::ui {

// StatusPill: dot + word. Colour never carries the state alone.
class StatusPill : public QWidget {
	Q_OBJECT
public:
	enum class Tone { Neutral, Busy, Live, Warn, Ok };
	explicit StatusPill(QWidget *parent = nullptr);
	void set(Tone tone, const QString &text);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *) override;

private:
	Tone m_tone = Tone::Neutral;
	QString m_text;
	QTimer m_blink;
	bool m_blinkOn = true;
};

// Round avatar with initials fallback and an optional live lamp.
class Avatar : public QWidget {
	Q_OBJECT
public:
	explicit Avatar(QWidget *parent = nullptr);
	void setImage(const QImage &image);
	void setName(const QString &name);
	void setLive(bool live);
	QSize sizeHint() const override { return {40, 40}; }

protected:
	void paintEvent(QPaintEvent *) override;

private:
	QImage m_image;
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
	QSize sizeHint() const override { return {184, 184}; }

signals:
	void clicked();

protected:
	void paintEvent(QPaintEvent *) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

private:
	std::vector<bool> m_modules;
	int m_size = 0;
	Overlay m_overlay = Overlay::Loading;
	QString m_overlayText;
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

// Inline banner with icon, text and up to two actions.
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

private:
	Tone m_tone = Tone::Info;
	QString m_iconName;
	QLabel *m_icon;
	QLabel *m_text;
	QPushButton *m_primary;
	QPushButton *m_secondary;
};

// Toast pinned to the bottom of the dock.
class Toast : public QFrame {
	Q_OBJECT
public:
	explicit Toast(QWidget *parent);
	void showMessage(int kind, const QString &text, const QString &detail);
	void reposition();

protected:
	void mousePressEvent(QMouseEvent *event) override;

private:
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
