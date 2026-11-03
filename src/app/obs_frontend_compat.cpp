// SPDX-License-Identifier: GPL-2.0-only
/*
Kaibo — Bilibili live control for OBS Studio
Copyright (C) 2026 Kaibo contributors
Based on obs-bilibili-stream by Zaros and contributors.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation. See LICENSE.
*/

#include "app/obs_frontend_compat.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>
#include <QWidget>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace kaibo::obsfrontend {
namespace {

using AddCustomDockFn = bool (*)(const char *id, void *dock);
using AddDockByIdFn = bool (*)(const char *id, const char *title, void *widget);
using AddDockFn = void *(*)(void *dock);

// Resolve an obs-frontend-api export at runtime instead of importing it.
// OBS 30/31/32/33 ship different subsets of the dock APIs (see DockApi), so an
// import table entry for any of them breaks plugin loading on some versions.
void *frontendSymbol(const char *name)
{
#if defined(_WIN32)
	HMODULE module = GetModuleHandleW(L"obs-frontend-api.dll");
	return module ? reinterpret_cast<void *>(GetProcAddress(module, name)) : nullptr;
#else
	return dlsym(RTLD_DEFAULT, name);
#endif
}

const char *apiName(DockApi api)
{
	switch (api) {
	case DockApi::AddCustomDock:
		return "obs_frontend_add_custom_qdock";
	case DockApi::AddDockById:
		return "obs_frontend_add_dock_by_id";
	case DockApi::LegacyAddDock:
		return "obs_frontend_add_dock";
	case DockApi::None:
		break;
	}
	return "none";
}

QDockWidget *createDock(QMainWindow *mainWindow, const char *dockId, const QString &title)
{
	auto *dock = new QDockWidget(title, mainWindow);
	dock->setObjectName(QString::fromUtf8(dockId));
	dock->setAllowedAreas(Qt::AllDockWidgetAreas);
	dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
			  QDockWidget::DockWidgetFloatable);
	return dock;
}

DockRegistration succeed(DockApi api, QDockWidget *dock, QWidget *content, const char *dockId)
{
	dock->setWidget(content);
	obs_log(LOG_INFO, "dock '%s' registered with %s", dockId, apiName(api));
	return DockRegistration{api, dock};
}

// OBS 30+: hand OBS a dock created by the plugin. OBS adds it to the right
// dock area as a regular visible dock, unlike obs_frontend_add_dock_by_id()
// which also hides and floats the OBS-created dock.
DockRegistration registerCustomDock(AddCustomDockFn addCustomDock, QMainWindow *mainWindow, QWidget *content,
				    const char *dockId, const QString &title)
{
	QDockWidget *dock = createDock(mainWindow, dockId, title);
	if (!addCustomDock(dockId, dock)) {
		// OBS rejected the id (usually because another copy of the plugin
		// already registered it). Our dock was not added, so discard it.
		delete dock;
		return {};
	}
	return succeed(DockApi::AddCustomDock, dock, content, dockId);
}

// OBS 30+: let OBS create the dock and wrap our widget. Used only when
// obs_frontend_add_custom_qdock is unavailable or explicitly requested.
DockRegistration registerDockById(AddDockByIdFn addDockById, QWidget *content, const char *dockId, const QString &title)
{
	const QByteArray utf8Title = title.toUtf8();
	if (!addDockById(dockId, utf8Title.constData(), content))
		return {};

	// OBS reparents the widget into its own dock while registering it.
	auto *dock = qobject_cast<QDockWidget *>(content->parentWidget());
	if (!dock) {
		obs_log(LOG_WARNING, "dock '%s' was registered but its QDockWidget could not be found", dockId);
		return {};
	}
	return succeed(DockApi::AddDockById, dock, content, dockId);
}

// OBS 28 - 31: the legacy API only adds a Docks menu entry for an existing
// QDockWidget; the plugin has to add the dock to the main window itself.
DockRegistration registerLegacyDock(AddDockFn addDock, QMainWindow *mainWindow, QWidget *content, const char *dockId,
				    const QString &title)
{
	QDockWidget *dock = createDock(mainWindow, dockId, title);
	mainWindow->addDockWidget(Qt::RightDockWidgetArea, dock);
	if (!addDock(dock))
		obs_log(LOG_WARNING, "dock '%s' has no Docks menu entry (obs_frontend_add_dock failed)", dockId);
	return succeed(DockApi::LegacyAddDock, dock, content, dockId);
}

// KAIBO_DOCK_API can pin the registration path ("custom", "by-id", "legacy") to
// compare dock behaviour between OBS versions without rebuilding the plugin.
DockApi requestedApi()
{
	const QString value = qEnvironmentVariable("KAIBO_DOCK_API").trimmed().toLower();
	if (value == QStringLiteral("custom"))
		return DockApi::AddCustomDock;
	if (value == QStringLiteral("by-id") || value == QStringLiteral("by_id"))
		return DockApi::AddDockById;
	if (value == QStringLiteral("legacy"))
		return DockApi::LegacyAddDock;
	return DockApi::None;
}

} // namespace

DockRegistration registerDock(QMainWindow *mainWindow, QWidget *content, const char *dockId, const QString &title)
{
	if (!mainWindow || !content || !dockId) {
		obs_log(LOG_ERROR, "cannot register the dock without a main window and a widget");
		return {};
	}

	const auto addCustomDock = reinterpret_cast<AddCustomDockFn>(frontendSymbol("obs_frontend_add_custom_qdock"));
	const auto addDockById = reinterpret_cast<AddDockByIdFn>(frontendSymbol("obs_frontend_add_dock_by_id"));
	const auto addDock = reinterpret_cast<AddDockFn>(frontendSymbol("obs_frontend_add_dock"));

	const DockApi requested = requestedApi();
	if (requested != DockApi::None) {
		DockRegistration registration;
		bool available = true;
		switch (requested) {
		case DockApi::AddCustomDock:
			available = addCustomDock != nullptr;
			if (available)
				registration = registerCustomDock(addCustomDock, mainWindow, content, dockId, title);
			break;
		case DockApi::AddDockById:
			available = addDockById != nullptr;
			if (available)
				registration = registerDockById(addDockById, content, dockId, title);
			break;
		case DockApi::LegacyAddDock:
			available = addDock != nullptr;
			if (available)
				registration = registerLegacyDock(addDock, mainWindow, content, dockId, title);
			break;
		case DockApi::None:
			break;
		}
		if (registration)
			return registration;
		obs_log(LOG_WARNING, "KAIBO_DOCK_API=%s is not usable by this OBS version; using automatic selection",
			apiName(requested));
	}

	// The plugin owns the QDockWidget on every path that supports it, so the
	// dock is visible and docked by default on OBS 30+. OBS 28/29 only have the
	// legacy API, which needs the plugin to add the dock to the main window.
	if (addCustomDock) {
		DockRegistration registration = registerCustomDock(addCustomDock, mainWindow, content, dockId, title);
		if (registration)
			return registration;
	}
	if (addDockById) {
		DockRegistration registration = registerDockById(addDockById, content, dockId, title);
		if (registration)
			return registration;
	}
	if (addDock)
		return registerLegacyDock(addDock, mainWindow, content, dockId, title);

	obs_log(LOG_ERROR, "this OBS version has no supported frontend dock API (obs_frontend_add_dock family)");
	return {};
}

void showDock(QDockWidget *dock)
{
	if (!dock)
		return;
	dock->show();
	dock->raise();
}

bool ensureDockManaged(QMainWindow *mainWindow, QDockWidget *dock, const char *dockId, bool firstRun)
{
	if (!mainWindow || !dock)
		return false;
	if (!firstRun && (dock->isVisible() || mainWindow->dockWidgetArea(dock) != Qt::NoDockWidgetArea))
		return false;

	mainWindow->addDockWidget(Qt::RightDockWidgetArea, dock);
	dock->setFloating(false);
	dock->show();
	dock->raise();
	obs_log(LOG_INFO, "dock '%s' was missing from the main window layout; added it to the right dock area",
		dockId ? dockId : "kaibo");
	return true;
}

bool showDockFullHeightRight(QMainWindow *mainWindow, const char *dockId)
{
	if (!mainWindow || !dockId)
		return false;

	auto *dock = mainWindow->findChild<QDockWidget *>(QString::fromUtf8(dockId));
	if (!dock) {
		obs_log(LOG_WARNING, "cannot find the '%s' dock to move it", dockId);
		return false;
	}

	mainWindow->addDockWidget(Qt::RightDockWidgetArea, dock);
	dock->show();
	dock->raise();

	// OBS 33 added a "Full Height Docks" toggle that gives the side dock areas
	// the window corners, which lets a right dock span the whole window height.
	// Older OBS versions have no such action, so this is simply skipped there.
	// Changing the setting is fine because the user explicitly asked for it.
	if (auto *sideDocks = mainWindow->findChild<QAction *>(QStringLiteral("sideDocks"));
	    sideDocks && !sideDocks->isChecked())
		sideDocks->trigger();

	obs_log(LOG_INFO, "dock '%s' placed in the full-height right dock area", dockId);
	return true;
}

} // namespace kaibo::obsfrontend
