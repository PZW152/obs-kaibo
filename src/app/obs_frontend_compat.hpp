// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QString>

class QDockWidget;
class QMainWindow;
class QWidget;

namespace kaibo::obsfrontend {

// Dock registration APIs exported by the OBS frontend. None of them may be
// imported statically: obs_frontend_add_dock was removed in OBS 32 and
// obs_frontend_add_dock_by_id does not exist before OBS 30, so a static import
// of either one makes the plugin fail to load on at least one OBS version.
enum class DockApi {
	None,
	AddCustomDock, // obs_frontend_add_custom_qdock (OBS 30+)
	AddDockById,   // obs_frontend_add_dock_by_id (OBS 30+)
	LegacyAddDock, // obs_frontend_add_dock (OBS 28 - 31)
};

struct DockRegistration {
	explicit operator bool() const { return dockWidget != nullptr; }

	DockApi api = DockApi::None;
	QDockWidget *dockWidget = nullptr;
};

// Creates a dock holding `content` and registers it with the OBS frontend using
// the newest API the host exports. The persistent object name stays
// `dockId`, so dock layouts saved by earlier plugin versions keep restoring.
DockRegistration registerDock(QMainWindow *mainWindow, QWidget *content, const char *dockId, const QString &title);

// Shows and raises a dock returned by registerDock().
void showDock(QDockWidget *dock);

// Idempotent safety net for the first start with no saved layout: OBS's own
// layout restore can drop a plugin-registered dock from the main window, leaving
// new users with no visible panel. `firstRun` is true only once per install (the
// caller persists it), so a dock that existing users closed stays closed and
// only a brand new install gets the dock shown.
bool ensureDockManaged(QMainWindow *mainWindow, QDockWidget *dock, const char *dockId, bool firstRun);

// User-triggered fix for OBS 33's layout: moves the dock back to the right dock
// area, shows it, and - if the OBS version has the "full height docks" toggle -
// enables that toggle so the right column can span the whole window height.
bool showDockFullHeightRight(QMainWindow *mainWindow, const char *dockId);

} // namespace kaibo::obsfrontend
