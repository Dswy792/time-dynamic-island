#pragma once

#include <windows.h>

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
