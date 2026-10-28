#pragma once

#include <windows.h>

// Commands posted by the tray icon menu through WM_COMMAND.
enum TrayCommand
{
    TRAY_CMD_SETTINGS = 40001,
    TRAY_CMD_TIME_SETTINGS = 40002,
    TRAY_CMD_AUTOSTART = 40003,
    TRAY_CMD_RESET_POSITION = 40004,
    TRAY_CMD_ABOUT = 40005,
    TRAY_CMD_EXIT = 40006
};

void InitTrayIcon(HWND hwnd);
void DestroyTrayIcon();

// Re-add the icon after the Explorer taskbar was restarted.
void ReinstallTrayIcon();

// Refresh the icon tooltip text (current time + state).
void UpdateTrayIcon();

// Handle the kTrayCallbackMessage notification from the shell.
void HandleTrayCallback(WPARAM wParam, LPARAM lParam);
