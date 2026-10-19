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
#include "app/worker.hpp"
#include "ui/dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>
#include <QPointer>

#include <utility>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

constexpr const char *kDockId = "kaibo";
constexpr const char *kHotkeyGoLive = "kaibo.golive";
constexpr const char *kHotkeyEndLive = "kaibo.endlive";

QPointer<kaibo::LiveController> g_controller;
QPointer<kaibo::ui::KaiboDock> g_dock;
obs_hotkey_id g_goLiveHotkey = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id g_endLiveHotkey = OBS_INVALID_HOTKEY_ID;

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	kaibo::LiveController *controller = g_controller.data();
	if (!controller)
		return;
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		controller->startup();
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
	g_controller = new kaibo::LiveController(mainWindow);
	g_dock = new kaibo::ui::KaiboDock(g_controller.data());
	if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("Dock.Title"), g_dock.data())) {
		obs_log(LOG_ERROR, "could not add the Kaibo dock");
		delete g_dock.data();
		return false;
	}

	if (auto *action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Menu.Tools")))) {
		QObject::connect(action, &QAction::triggered, [] {
			if (!g_dock)
				return;
			if (auto *dock = qobject_cast<QDockWidget *>(g_dock->parentWidget())) {
				dock->show();
				dock->raise();
			}
		});
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
