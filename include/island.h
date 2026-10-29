#pragma once

#include <windows.h>

#include "notifications.h"

// Application level commands shared by the tray icon and settings window.

// Re-apply g_settings: validate, recompute geometry, refresh the tray icon and
// repaint the island. Does not write the registry.
void ApplySettingsAndRefresh();

// Reset the island to the configured preset position (clears the dragged
// custom position), save it and refresh.
void ResetIslandPosition();

// Ask the main window to exit the application cleanly.
void RequestApplicationExit();

// Re-read the system theme and repaint everything when it changed.
void RefreshIslandFromSystemTheme();

// Called by the notifications module (UI thread) for every new WeChat/QQ
// message: pops out / refreshes the circular sub-island of that source.
void ShowNotificationSubIsland(const AppNotification& notification);

// Hides the sub-island of the given source (after its dwell time or a click).
void DismissNotificationSubIsland(NotificationSource source);

// True while at least one sub-island is visible or animating.
bool IsSubIslandActive();

// Bring the WeChat/QQ main window to the foreground (used when a sub-island is
// clicked).
void ActivateSourceApp(NotificationSource source);

// Ask Windows for notification access; shows the system consent dialog once.
void RequestNotificationAccessFromUi();
