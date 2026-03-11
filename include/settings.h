#pragma once

#include <windows.h>

void LoadSettings();
void SaveSettings();
INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
void ShowSettingsDialog();
