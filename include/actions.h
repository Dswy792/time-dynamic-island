#pragma once

#include <windows.h>

// Open the Windows "Date & time" control panel page.
void OpenDateAndTimeSettings();

// Show the about dialog (version, links).
void ShowAboutDialog(HWND owner);

// True when the foreground window covers an entire monitor (borderless
// fullscreen games / videos). Used by the optional "hide in fullscreen" mode.
bool IsFullscreenAppActive();
