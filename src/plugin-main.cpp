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
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QMainWindow>
#include <QPointer>
#include <QTimer>

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
bool g_dockFirstRun = false;
bool g_dockFirstRunResolved = false;
obs_hotkey_id g_goLiveHotkey = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id g_endLiveHotkey = OBS_INVALID_HOTKEY_ID;

// No-op unless OBS's layout restore dropped the dock from the main window (this
// happens on the first start, where a new user would otherwise see no panel).
// The marker file makes that force-show a one-time event per installation, so a
// dock that an existing user closed is not re-opened on every start.
void ensureDockManaged()
{
	if (!g_mainWindow || !g_dockWidget)
		return;
	if (!g_dockFirstRunResolved && g_controller) {
		const QString dataDir = g_controller->storageDirectory();
		const QString marker = QDir(dataDir).filePath(QStringLiteral(".dock-first-run"));
		g_dockFirstRun = !QFileInfo::exists(marker);
		g_dockFirstRunResolved = true;
		QDir().mkpath(dataDir);
		QFile file(marker);
		if (file.open(QIODevice::WriteOnly))
			file.close();
	}
	kaibo::obsfrontend::ensureDockManaged(g_mainWindow.data(), g_dockWidget.data(), kDockId, g_dockFirstRun);
}

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	kaibo::LiveController *controller = g_controller.data();
	if (!controller)
		return;
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		controller->startup();
		// OBS applies the saved dock layout around this event, so check once
		// now and once after the event loop has settled.
		ensureDockManaged();
		QTimer::singleShot(200, [] { ensureDockManaged(); });
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
