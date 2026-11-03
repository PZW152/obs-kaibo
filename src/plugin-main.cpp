// SPDX-License-Identifier: GPL-2.0-only
/*
Kaibo — Bilibili live control for OBS Studio
Copyright (C) 2026 Kaibo contributors
Based on obs-bilibili-stream by Zaros and contributors.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation. See LICENSE.
*/

#include "app/controller.hpp"
#include "app/i18n.hpp"
#include "app/obs_frontend_compat.hpp"
#include "app/worker.hpp"
#include "ui/dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QMainWindow>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <utility>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

constexpr const char *kDockId = "kaibo";
constexpr const char *kHotkeyGoLive = "kaibo.golive";
constexpr const char *kHotkeyEndLive = "kaibo.endlive";

QPointer<kaibo::LiveController> g_controller;
QPointer<kaibo::ui::KaiboDock> g_dock;
QPointer<QDockWidget> g_dockWidget;
QPointer<QMainWindow> g_mainWindow;
bool g_dockFirstRunKnown = false;
bool g_dockFirstRun = false;
bool g_dockMarkerWritten = false;
obs_hotkey_id g_goLiveHotkey = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id g_endLiveHotkey = OBS_INVALID_HOTKEY_ID;

// No-op unless OBS's layout restore dropped the dock from the main window (this
// happens on the first start, where a new user would otherwise see no panel).
// The marker file makes that force-show a one-time event per installation, so a
// dock that an existing user closed is not re-opened on every start. The marker
// is written only once the dock is actually visible.
void ensureDockManaged()
{
	if (!g_mainWindow || !g_dockWidget || !g_controller)
		return;
	const QString dataDir = g_controller->storageDirectory();
	const QString marker = QDir(dataDir).filePath(QStringLiteral(".dock-first-run"));
	if (!g_dockFirstRunKnown) {
		g_dockFirstRun = !QFileInfo::exists(marker);
		g_dockFirstRunKnown = true;
	}
	kaibo::obsfrontend::ensureDockManaged(g_mainWindow.data(), g_dockWidget.data(), kDockId, g_dockFirstRun);
	if (g_dockFirstRun && !g_dockMarkerWritten && g_dockWidget->isVisible()) {
		QDir().mkpath(dataDir);
		QFile file(marker);
		if (file.open(QIODevice::WriteOnly)) {
			file.close();
			g_dockMarkerWritten = true;
		}
	}
}

// First start on a brand-new OBS: OBS's first-run wizard is still open and the
// main window is not focused yet, so adding the dock during FINISHED_LOADING
// leaves it floating above the wizard. Only place it once the user actually has
// the main window focused and no dialog (wizard, OBS message box) is open.
bool dockPlacementBlocked()
{
	if (!g_mainWindow || !g_mainWindow->isVisible())
		return true;
	if (QApplication::activeModalWidget() != nullptr)
		return true;
	for (QWidget *widget : QApplication::topLevelWidgets()) {
		if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->isVisible())
			return true;
	}
	return false;
}

void placeFirstRunDock()
{
	if (!g_dockFirstRunKnown) {
		const QString dataDir = g_controller ? g_controller->storageDirectory() : QString();
		g_dockFirstRun = dataDir.isEmpty() ||
				 !QFileInfo::exists(QDir(dataDir).filePath(QStringLiteral(".dock-first-run")));
		g_dockFirstRunKnown = true;
	}
	if (!g_dockFirstRun || g_dockMarkerWritten || dockPlacementBlocked())
		return;
	ensureDockManaged();
}

// Retrigger the placement when the main window is activated (the wizard just
// closed) or shown; the timers below are a fallback for hosts that do neither.
class DockPlacementFilter : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override
	{
		if (watched == g_mainWindow.data() &&
		    (event->type() == QEvent::WindowActivate || event->type() == QEvent::Show)) {
			QTimer::singleShot(300, [] { placeFirstRunDock(); });
		}
		return QObject::eventFilter(watched, event);
	}
};

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	kaibo::LiveController *controller = g_controller.data();
	if (!controller)
		return;
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		controller->startup();
		// OBS applies the saved dock layout around this event (and can drop the
		// dock again a moment later). On the very first start OBS opens its
		// first-run wizard right after this event, so do not place the dock
		// immediately: wait until the wizard is closed and the main window is
		// focused, otherwise the dock would stay a floating window.
		if (!g_dockFirstRunKnown) {
			const QString dataDir = g_controller->storageDirectory();
			g_dockFirstRun = !QFileInfo::exists(QDir(dataDir).filePath(QStringLiteral(".dock-first-run")));
			g_dockFirstRunKnown = true;
		}
		if (g_dockFirstRun && !g_dockMarkerWritten) {
			if (g_mainWindow)
				g_mainWindow->installEventFilter(new DockPlacementFilter(g_mainWindow.data()));
			for (int delay : {3000, 6000, 12000, 20000, 30000})
				QTimer::singleShot(delay, [] { placeFirstRunDock(); });
		} else {
			// Existing installation: repair a dock that OBS's layout dropped.
			ensureDockManaged();
			QTimer::singleShot(250, [] { ensureDockManaged(); });
		}
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
		controller->onStreamingStarting();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		controller->onStreamingStarted();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		controller->onStreamingStopped();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		controller->shutdownForExit();
		break;
	default:
		break;
	}
}

void onHotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed || !g_dock)
		return;
	const bool goLive = data != nullptr;
	// Hotkeys fire on OBS's hotkey thread.
	kaibo::worker::postToUi(g_dock.data(), [goLive] {
		if (!g_dock)
			return;
		if (goLive)
			g_dock->requestGoLive();
		else
			g_dock->requestEndLive();
	});
}

void onSave(obs_data_t *data, bool saving, void *)
{
	const std::pair<const char *, obs_hotkey_id> hotkeys[] = {{kHotkeyGoLive, g_goLiveHotkey},
								  {kHotkeyEndLive, g_endLiveHotkey}};
	for (const auto &[key, id] : hotkeys) {
		if (id == OBS_INVALID_HOTKEY_ID)
			continue;
		if (saving) {
			obs_data_array_t *array = obs_hotkey_save(id);
			obs_data_set_array(data, key, array);
			obs_data_array_release(array);
		} else {
			obs_data_array_t *array = obs_data_get_array(data, key);
			obs_hotkey_load(id, array);
			obs_data_array_release(array);
		}
	}
}

} // namespace

const char *obs_module_name(void)
{
	return "Kaibo";
}

const char *obs_module_description(void)
{
	return obs_module_text("Plugin.Description");
}

bool obs_module_load(void)
{
	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mainWindow) {
		obs_log(LOG_ERROR, "no OBS main window; Kaibo needs the OBS Studio frontend");
		return false;
	}

	kaibo::worker::start();
	g_mainWindow = mainWindow;
	g_controller = new kaibo::LiveController(mainWindow);
	g_dock = new kaibo::ui::KaiboDock(g_controller.data());
	const kaibo::obsfrontend::DockRegistration dock =
		kaibo::obsfrontend::registerDock(mainWindow, g_dock.data(), kDockId, obs_module_text("Dock.Title"));
	if (!dock) {
		obs_log(LOG_ERROR, "could not add the Kaibo dock");
		delete g_dock.data();
		// OBS does not call obs_module_unload() when load fails, so clean up here.
		kaibo::worker::shutdown();
		delete g_controller.data();
		return false;
	}
	g_dockWidget = dock.dockWidget;

	if (auto *action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Menu.Tools")))) {
		QObject::connect(action, &QAction::triggered, [] {
			ensureDockManaged();
			if (g_dockWidget)
				kaibo::obsfrontend::showDock(g_dockWidget.data());
		});
	}

	// Second Tools entry: user-initiated fix for OBS 33 layouts where the right
	// dock column does not reach the top of the window.
	if (auto *action =
		    static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Menu.FullHeight")))) {
		QObject::connect(action, &QAction::triggered,
				 [mainWindow] { kaibo::obsfrontend::showDockFullHeightRight(mainWindow, kDockId); });
	}

	// data != nullptr marks the go-live hotkey.
	g_goLiveHotkey = obs_hotkey_register_frontend(kHotkeyGoLive, obs_module_text("Hotkey.GoLive"), onHotkey,
						      reinterpret_cast<void *>(1));
	g_endLiveHotkey =
		obs_hotkey_register_frontend(kHotkeyEndLive, obs_module_text("Hotkey.EndLive"), onHotkey, nullptr);
	obs_frontend_add_save_callback(onSave, nullptr);
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);

	obs_log(LOG_INFO, "loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	obs_frontend_remove_save_callback(onSave, nullptr);
	if (g_goLiveHotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(g_goLiveHotkey);
	if (g_endLiveHotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(g_endLiveHotkey);
	kaibo::worker::shutdown();
	// The dock and the controller are owned by OBS's main window and go with it.
	obs_log(LOG_INFO, "unloaded");
}
