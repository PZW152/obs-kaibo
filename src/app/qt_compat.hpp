// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QObject>
#include <QString>
#include <QVariant>

namespace kaibo {

// The Windows build uses newer Qt headers than the oldest OBS it supports ships. Calling
// QObject::setObjectName() there compiles to a symbol only Qt 6.4+ exports, and one missing
// symbol stops the whole module from loading on OBS 28 and 29.0. The property system reaches
// the same setter through a call every Qt 6 has. tools/check-obs-compat.py guards this.
inline void setObjectId(QObject *object, const QString &name)
{
	object->setProperty("objectName", name);
}

} // namespace kaibo
