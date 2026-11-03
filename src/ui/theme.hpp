// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QIcon>
#include <QPixmap>
#include <QString>

class QPalette;
class QWidget;

namespace kaibo::ui {

// Design tokens (data/theme/tokens.json) and the stylesheet built from them.
// The dock follows OBS: dark tokens when OBS's window colour is dark.
class Theme {
public:
	static Theme &instance();

	bool load();
	bool dark() const { return m_dark; }
	// Returns true when the mode changed.
	bool followPalette(const QPalette &palette);

	QColor color(const QString &token) const;
	int length(const QString &token, int fallback) const;
	QString styleSheet() const;
	QFont monoFont(int pixelSize = 12) const;

	// A 24px-grid stroke icon from data/icons, tinted with a token colour.
	// size is the logical edge in px; buttons ask for their own icon size so a
	// 16px control does not get an 18px bitmap upscaled/downscaled.
	QIcon icon(const QString &name, const QString &token = QStringLiteral("ink-muted"), int size = 18) const;
	// Rasterised and cached per (icon, colour, size, devicePixelRatio); paint
	// events may call this freely.
	QPixmap pixmap(const QString &name, const QColor &color, int size, qreal dpr) const;

	// Re-applies a dynamic property so the stylesheet picks it up.
	static void setProp(QWidget *widget, const char *name, const QVariant &value);

private:
	QHash<QString, QString> m_darkValues;
	QHash<QString, QString> m_lightValues;
	QHash<QString, QString> m_lengths;
	QString m_template;
	bool m_dark = true;
	bool m_loaded = false;
	mutable QHash<QString, QByteArray> m_iconSources;
	mutable QHash<QString, QPixmap> m_pixmapCache;
};

} // namespace kaibo::ui
