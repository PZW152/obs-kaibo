// SPDX-License-Identifier: GPL-2.0-only
#include "ui/theme.hpp"
#include "core/settings_model.hpp"
#include "vendor/json11/json11.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/bmem.h>

#include <QFile>
#include <QPainter>
#include <QPalette>
#include <QRegularExpression>
#include <QStyle>
#include <QSvgRenderer>
#include <QVariant>
#include <QWidget>

namespace kaibo::ui {
namespace {

QString moduleFile(const char *relative)
{
	char *raw = obs_module_file(relative);
	QString path = raw ? QString::fromUtf8(raw) : QString();
	bfree(raw);
	return path;
}

QString readText(const QString &path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return QString::fromUtf8(file.readAll());
}

// Accepts #rgb/#rrggbb/#rrggbbaa and rgb()/rgba() with plain numbers.
QColor parseColor(const QString &value)
{
	const QString v = value.trimmed();
	if (v.startsWith('#')) {
		if (v.size() == 9) { // #rrggbbaa → QColor wants #aarrggbb
			QColor c(QStringLiteral("#") + v.mid(7, 2) + v.mid(1, 6));
			return c;
		}
		return QColor(v);
	}
	static const QRegularExpression rgba(
		QStringLiteral(R"(^rgba?\(\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*(?:,\s*([\d.]+)\s*)?\)$)"));
	const auto m = rgba.match(v);
	if (m.hasMatch()) {
		QColor c(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
		if (!m.captured(4).isEmpty())
			c.setAlphaF(m.captured(4).toDouble());
		return c;
	}
	return QColor();
}

} // namespace

Theme &Theme::instance()
{
	static Theme theme;
	return theme;
}

bool Theme::load()
{
	if (m_loaded)
		return true;
	const QString tokensText = readText(moduleFile("theme/tokens.json"));
	m_template = readText(moduleFile("theme/kaibo.qss"));
	std::string err;
	const auto tokens = json11::Json::parse(tokensText.toStdString(), err);
	if (!err.empty() || m_template.isEmpty()) {
		obs_log(LOG_ERROR, "theme files missing or invalid (%s); the dock will use OBS's style", err.c_str());
		return false;
	}

	for (const auto &token : tokens["color"]["tokens"].array_items()) {
		const QString name = QString::fromStdString(token["name"].string_value());
		const auto &value = token["value"];
		if (value.is_string()) {
			m_darkValues[name] = m_lightValues[name] = QString::fromStdString(value.string_value());
		} else {
			m_darkValues[name] = QString::fromStdString(value["dark"].string_value());
			m_lightValues[name] = QString::fromStdString(value["light"].string_value());
		}
	}
	for (const char *family : {"spacing", "radius", "size"})
		for (const auto &token : tokens[family]["tokens"].array_items())
			m_lengths[QString::fromStdString(token["name"].string_value())] =
				QString::fromStdString(token["value"].string_value());
	m_loaded = true;
	return true;
}

bool Theme::followPalette(const QPalette &palette)
{
	const bool dark = palette.color(QPalette::Window).lightness() < 128;
	if (dark == m_dark)
		return false;
	m_dark = dark;
	return true;
}

QColor Theme::color(const QString &token) const
{
	const auto &values = m_dark ? m_darkValues : m_lightValues;
	const QColor c = parseColor(values.value(token));
	return c.isValid() ? c : QColor(m_dark ? Qt::white : Qt::black);
}

int Theme::length(const QString &token, int fallback) const
{
	QString v = m_lengths.value(token);
	v.remove(QStringLiteral("px"));
	bool ok = false;
	const int n = v.toInt(&ok);
	return ok ? n : fallback;
}

QString Theme::styleSheet() const
{
	if (!m_loaded)
		return {};
	QString sheet = m_template;
	const auto &values = m_dark ? m_darkValues : m_lightValues;
	for (auto it = values.begin(); it != values.end(); ++it)
		sheet.replace(QLatin1Char('@') + it.key() + QLatin1Char('@'), it.value());
	for (auto it = m_lengths.begin(); it != m_lengths.end(); ++it)
		sheet.replace(QLatin1Char('@') + it.key() + QLatin1Char('@'), it.value());
	return sheet;
}

QFont Theme::monoFont(int pixelSize) const
{
	QFont font;
	font.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("JetBrains Mono"), QStringLiteral("SF Mono"),
			  QStringLiteral("Menlo"), QStringLiteral("Consolas"), QStringLiteral("monospace")});
	font.setStyleHint(QFont::Monospace);
	font.setPixelSize(pixelSize);
	return font;
}

QPixmap Theme::pixmap(const QString &name, const QColor &color, int size, qreal dpr) const
{
	QByteArray source = m_iconSources.value(name);
	if (source.isEmpty()) {
		QFile file(moduleFile(("icons/" + name + ".svg").toUtf8().constData()));
		if (!file.open(QIODevice::ReadOnly))
			return {};
		source = file.readAll();
		m_iconSources.insert(name, source);
	}
	QByteArray tinted = source;
	tinted.replace("currentColor", color.name(QColor::HexRgb).toUtf8());
	QSvgRenderer renderer(tinted);
	if (!renderer.isValid())
		return {};
	QPixmap pm(QSize(size, size) * dpr);
	pm.fill(Qt::transparent);
	pm.setDevicePixelRatio(dpr);
	QPainter painter(&pm);
	painter.setRenderHint(QPainter::Antialiasing);
	renderer.render(&painter, QRectF(0, 0, size, size));
	return pm;
}

QIcon Theme::icon(const QString &name, const QString &token) const
{
	QIcon icon;
	const QColor c = color(token);
	for (qreal dpr : {1.0, 1.5, 2.0, 3.0}) {
		const QPixmap pm = pixmap(name, c, 18, dpr);
		if (!pm.isNull())
			icon.addPixmap(pm);
	}
	return icon;
}

void Theme::setProp(QWidget *widget, const char *name, const QVariant &value)
{
	if (widget->property(name) == value)
		return;
	widget->setProperty(name, value);
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
	widget->update();
}

} // namespace kaibo::ui
