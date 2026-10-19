// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "core/text.hpp"

#include <obs-module.h>

#include <QString>

namespace kaibo {

// Localized text from data/locale/<OBS language>.ini (falls back to en-US).
inline QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

inline UiLocale uiLocale()
{
	const char *tag = obs_get_locale();
	return localeFromTag(tag ? tag : "en-US");
}

inline QString qs(const std::string &s)
{
	return QString::fromStdString(s);
}

inline std::string ss(const QString &s)
{
	return s.toStdString();
}

} // namespace kaibo
