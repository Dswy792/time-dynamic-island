#pragma once

#include <windows.h>

// Modeless, fully custom-drawn settings window (GDI+ layered window).
void ShowSettingsDialog(HWND owner);

// Close the window. When applyChanges is true the pending values are applied
// (equivalent to pressing Apply); otherwise they are discarded.
void CloseSettingsDialog(bool applyChanges);

bool IsSettingsDialogOpen();

// Repaint the window (theme change, external settings change, ...).
void RefreshSettingsDialog();
