#include "settings.h"

#include <windows.h>
#include <cstdlib>
#include <stdio.h>

#include "app_state.h"
#include "layout.h"
#include "resource.h"

static bool g_prevAutoHide = false;

void LoadSettings()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\DynamicIsland", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD size;
        size = sizeof(g_use24Hour);
        if (RegQueryValueExW(hKey, L"Use24Hour", NULL, NULL, reinterpret_cast<LPBYTE>(&g_use24Hour), &size) != ERROR_SUCCESS)
            g_use24Hour = true;  // 默认值
        
        size = sizeof(g_showDate);
        if (RegQueryValueExW(hKey, L"ShowDate", NULL, NULL, reinterpret_cast<LPBYTE>(&g_showDate), &size) != ERROR_SUCCESS)
            g_showDate = true;  // 默认值
        
        size = sizeof(g_showYear);
        if (RegQueryValueExW(hKey, L"ShowYear", NULL, NULL, reinterpret_cast<LPBYTE>(&g_showYear), &size) != ERROR_SUCCESS)
            g_showYear = false;  // 默认值
        
        size = sizeof(g_showSeconds);
        if (RegQueryValueExW(hKey, L"ShowSeconds", NULL, NULL, reinterpret_cast<LPBYTE>(&g_showSeconds), &size) != ERROR_SUCCESS)
            g_showSeconds = true;  // 默认值
        
        size = sizeof(g_theme);
        if (RegQueryValueExW(hKey, L"Theme", NULL, NULL, reinterpret_cast<LPBYTE>(&g_theme), &size) != ERROR_SUCCESS)
            g_theme = 0;  // 默认值：深色主题
        
        size = sizeof(g_autoHide);
        if (RegQueryValueExW(hKey, L"AutoHide", NULL, NULL, reinterpret_cast<LPBYTE>(&g_autoHide), &size) != ERROR_SUCCESS)
            g_autoHide = false;  // 默认值
        
        size = sizeof(g_windowSize);
        if (RegQueryValueExW(hKey, L"WindowSize", NULL, NULL, reinterpret_cast<LPBYTE>(&g_windowSize), &size) != ERROR_SUCCESS)
            g_windowSize = 1;  // 默认值：Medium
        
        size = sizeof(g_position);
        if (RegQueryValueExW(hKey, L"Position", NULL, NULL, reinterpret_cast<LPBYTE>(&g_position), &size) != ERROR_SUCCESS)
            g_position = 1;  // 默认值：Center
        
        size = sizeof(g_hideDelayMinutes);
        if (RegQueryValueExW(hKey, L"HideDelayMinutes", NULL, NULL, reinterpret_cast<LPBYTE>(&g_hideDelayMinutes), &size) != ERROR_SUCCESS)
            g_hideDelayMinutes = 0;  // 默认值
        
        size = sizeof(g_hideDelaySeconds);
        if (RegQueryValueExW(hKey, L"HideDelaySeconds", NULL, NULL, reinterpret_cast<LPBYTE>(&g_hideDelaySeconds), &size) != ERROR_SUCCESS)
            g_hideDelaySeconds = 1;  // 默认值
        
        size = sizeof(g_fontSize);
        if (RegQueryValueExW(hKey, L"FontSize", NULL, NULL, reinterpret_cast<LPBYTE>(&g_fontSize), &size) != ERROR_SUCCESS)
            g_fontSize = 14;  // 默认值
        
        size = sizeof(g_boldFont);
        if (RegQueryValueExW(hKey, L"BoldFont", NULL, NULL, reinterpret_cast<LPBYTE>(&g_boldFont), &size) != ERROR_SUCCESS)
            g_boldFont = true;  // 默认值
        
        RegCloseKey(hKey);
    }
}

void SaveSettings()
{
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\DynamicIsland", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, L"Use24Hour", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&g_use24Hour), sizeof(g_use24Hour));
        RegSetValueExW(hKey, L"ShowDate", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&g_showDate), sizeof(g_showDate));
        RegSetValueExW(hKey, L"ShowYear", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&g_showYear), sizeof(g_showYear));
        RegSetValueExW(hKey, L"ShowSeconds", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&g_showSeconds), sizeof(g_showSeconds));
        RegSetValueExW(hKey, L"Theme", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&g_theme), sizeof(g_theme));
        RegSetValueExW(hKey, L"AutoHide", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&g_autoHide), sizeof(g_autoHide));
        RegSetValueExW(hKey, L"WindowSize", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&g_windowSize), sizeof(g_windowSize));
        RegSetValueExW(hKey, L"Position", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&g_position), sizeof(g_position));
        RegSetValueExW(hKey, L"HideDelayMinutes", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&g_hideDelayMinutes), sizeof(g_hideDelayMinutes));
        RegSetValueExW(hKey, L"HideDelaySeconds", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&g_hideDelaySeconds), sizeof(g_hideDelaySeconds));
        RegSetValueExW(hKey, L"FontSize", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&g_fontSize), sizeof(g_fontSize));
        RegSetValueExW(hKey, L"BoldFont", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&g_boldFont), sizeof(g_boldFont));
        RegCloseKey(hKey);
    }
}

INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        RECT rcDlg, rcScreen;
        GetWindowRect(hDlg, &rcDlg);

        SystemParametersInfo(SPI_GETWORKAREA, 0, &rcScreen, 0);

        int x = rcScreen.left + (rcScreen.right - rcScreen.left - (rcDlg.right - rcDlg.left)) / 2;
        int y = rcScreen.top + (rcScreen.bottom - rcScreen.top - (rcDlg.bottom - rcDlg.top)) / 2;

        SetWindowPos(hDlg, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE);

        if (g_theme < 0 || g_theme > 2)
            g_theme = 0;

        CheckRadioButton(hDlg, IDC_RADIO_24HOUR, IDC_RADIO_12HOUR,
                         g_use24Hour ? IDC_RADIO_24HOUR : IDC_RADIO_12HOUR);
        CheckDlgButton(hDlg, IDC_CHECK_DATE, g_showDate ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_SECONDS, g_showSeconds ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_YEAR, g_showYear ? BST_CHECKED : BST_UNCHECKED);

        CheckRadioButton(hDlg, IDC_RADIO_DARK, IDC_RADIO_SYSTEM,
                         g_theme == 0 ? IDC_RADIO_DARK : (g_theme == 1 ? IDC_RADIO_LIGHT : IDC_RADIO_SYSTEM));

        CheckRadioButton(hDlg, IDC_RADIO_SMALL, IDC_RADIO_LARGE,
                         g_windowSize == 0 ? IDC_RADIO_SMALL : (g_windowSize == 1 ? IDC_RADIO_MEDIUM : IDC_RADIO_LARGE));

        CheckRadioButton(hDlg, IDC_RADIO_LEFT, IDC_RADIO_RIGHT,
                         g_position == 0 ? IDC_RADIO_LEFT : (g_position == 1 ? IDC_RADIO_CENTER : IDC_RADIO_RIGHT));

        CheckDlgButton(hDlg, IDC_CHECK_AUTOHIDE, g_autoHide ? BST_CHECKED : BST_UNCHECKED);

        HWND hMinEdit = GetDlgItem(hDlg, IDC_EDIT_HIDEDELAY_MIN);
        wchar_t minText[32];
        swprintf_s(minText, 32, L"%d", g_hideDelayMinutes);
        SetWindowTextW(hMinEdit, minText);

        HWND hSecEdit = GetDlgItem(hDlg, IDC_EDIT_HIDEDELAY_SEC);
        wchar_t secText[32];
        swprintf_s(secText, 32, L"%d", g_hideDelaySeconds);
        SetWindowTextW(hSecEdit, secText);

        HWND hFontEdit = GetDlgItem(hDlg, IDC_EDIT_FONTSIZE);
        wchar_t fontSizeText[32];
        swprintf_s(fontSizeText, 32, L"%d", g_fontSize);
        SetWindowTextW(hFontEdit, fontSizeText);

        CheckDlgButton(hDlg, IDC_CHECK_BOLD, g_boldFont ? BST_CHECKED : BST_UNCHECKED);

        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
        {
            bool oldAutoHide = g_autoHide;
            
            // 先读取所有新值
            bool newUse24Hour = (IsDlgButtonChecked(hDlg, IDC_RADIO_24HOUR) == BST_CHECKED);
            bool newShowDate = (IsDlgButtonChecked(hDlg, IDC_CHECK_DATE) == BST_CHECKED);
            bool newShowSeconds = (IsDlgButtonChecked(hDlg, IDC_CHECK_SECONDS) == BST_CHECKED);
            bool newShowYear = (IsDlgButtonChecked(hDlg, IDC_CHECK_YEAR) == BST_CHECKED);
            int newTheme = IsDlgButtonChecked(hDlg, IDC_RADIO_DARK) == BST_CHECKED ? 0 : (IsDlgButtonChecked(hDlg, IDC_RADIO_LIGHT) == BST_CHECKED ? 1 : 2);
            int newWindowSize = IsDlgButtonChecked(hDlg, IDC_RADIO_SMALL) == BST_CHECKED ? 0 : (IsDlgButtonChecked(hDlg, IDC_RADIO_MEDIUM) == BST_CHECKED ? 1 : 2);
            int newPosition = IsDlgButtonChecked(hDlg, IDC_RADIO_LEFT) == BST_CHECKED ? 0 : (IsDlgButtonChecked(hDlg, IDC_RADIO_CENTER) == BST_CHECKED ? 1 : 2);
            bool newAutoHide = (IsDlgButtonChecked(hDlg, IDC_CHECK_AUTOHIDE) == BST_CHECKED);
            
            wchar_t minText[32];
            GetWindowTextW(GetDlgItem(hDlg, IDC_EDIT_HIDEDELAY_MIN), minText, 32);
            int newHideDelayMinutes = _wtoi(minText);
            
            wchar_t secText[32];
            GetWindowTextW(GetDlgItem(hDlg, IDC_EDIT_HIDEDELAY_SEC), secText, 32);
            int newHideDelaySeconds = _wtoi(secText);
            
            wchar_t fontSizeText[32];
            GetWindowTextW(GetDlgItem(hDlg, IDC_EDIT_FONTSIZE), fontSizeText, 32);
            int newFontSize = _wtoi(fontSizeText);
            
            bool newBoldFont = (IsDlgButtonChecked(hDlg, IDC_CHECK_BOLD) == BST_CHECKED);
            
            // 检查是否需要重建字体
            if (g_fontSize != newFontSize || g_boldFont != newBoldFont || g_showYear != newShowYear) {
                if (g_hFont != nullptr) {
                    DeleteObject(g_hFont);
                    g_hFont = nullptr;
                }
            }
            
            // 更新全局变量
            g_use24Hour = newUse24Hour;
            g_showDate = newShowDate;
            g_showSeconds = newShowSeconds;
            g_showYear = newShowYear;
            g_theme = newTheme;
            g_windowSize = newWindowSize;
            g_position = newPosition;
            g_autoHide = newAutoHide;
            g_hideDelayMinutes = newHideDelayMinutes;
            g_hideDelaySeconds = newHideDelaySeconds;
            g_fontSize = newFontSize;
            g_boldFont = newBoldFont;
            
            SaveSettings();
            
            if (oldAutoHide && !g_autoHide)
            {
                KillTimer(g_hwnd, 2);
                KillTimer(g_hwnd, 4);
                g_isHidden = false;
                g_isHovering = false;
                g_isAnimating = false;
                g_waitingForHide = false;
                g_isHiding = false;
                // 重置动画位置
                RECT workArea;
                SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
                g_animationY = workArea.top + 10;
            }

            if (!oldAutoHide && g_autoHide)
            {
                g_isHidden = false;
                g_isHovering = false;
                g_isAnimating = false;
                g_waitingForHide = false;
                g_isHiding = false;
                // 重置动画位置
                RECT workArea;
                SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
                g_animationY = workArea.top + 10;
                UpdateWindowPosition(false);
            }

            UpdateWindowPosition(g_isHovering);
            InvalidateRect(g_hwnd, NULL, TRUE);

            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        break;

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;

        case IDC_RADIO_DARK:
        case IDC_RADIO_LIGHT:
        case IDC_RADIO_SYSTEM:
            CheckRadioButton(hDlg, IDC_RADIO_DARK, IDC_RADIO_SYSTEM, LOWORD(wParam));
            return TRUE;

        case IDC_RADIO_24HOUR:
        case IDC_RADIO_12HOUR:
            CheckRadioButton(hDlg, IDC_RADIO_24HOUR, IDC_RADIO_12HOUR, LOWORD(wParam));
            return TRUE;

        case IDC_RADIO_SMALL:
        case IDC_RADIO_MEDIUM:
        case IDC_RADIO_LARGE:
            CheckRadioButton(hDlg, IDC_RADIO_SMALL, IDC_RADIO_LARGE, LOWORD(wParam));
            return TRUE;

        case IDC_RADIO_LEFT:
        case IDC_RADIO_CENTER:
        case IDC_RADIO_RIGHT:
            CheckRadioButton(hDlg, IDC_RADIO_LEFT, IDC_RADIO_RIGHT, LOWORD(wParam));
            return TRUE;

        case IDC_BTN_APPLY:
        {
            bool oldAutoHide2 = g_autoHide;
            
            // 先读取所有新值
            bool newUse24Hour = (IsDlgButtonChecked(hDlg, IDC_RADIO_24HOUR) == BST_CHECKED);
            bool newShowDate = (IsDlgButtonChecked(hDlg, IDC_CHECK_DATE) == BST_CHECKED);
            bool newShowSeconds = (IsDlgButtonChecked(hDlg, IDC_CHECK_SECONDS) == BST_CHECKED);
            bool newShowYear = (IsDlgButtonChecked(hDlg, IDC_CHECK_YEAR) == BST_CHECKED);
            int newTheme = IsDlgButtonChecked(hDlg, IDC_RADIO_DARK) == BST_CHECKED ? 0 : (IsDlgButtonChecked(hDlg, IDC_RADIO_LIGHT) == BST_CHECKED ? 1 : 2);
            int newWindowSize = IsDlgButtonChecked(hDlg, IDC_RADIO_SMALL) == BST_CHECKED ? 0 : (IsDlgButtonChecked(hDlg, IDC_RADIO_MEDIUM) == BST_CHECKED ? 1 : 2);
            int newPosition = IsDlgButtonChecked(hDlg, IDC_RADIO_LEFT) == BST_CHECKED ? 0 : (IsDlgButtonChecked(hDlg, IDC_RADIO_CENTER) == BST_CHECKED ? 1 : 2);
            bool newAutoHide = (IsDlgButtonChecked(hDlg, IDC_CHECK_AUTOHIDE) == BST_CHECKED);
            
            wchar_t minText2[32];
            GetWindowTextW(GetDlgItem(hDlg, IDC_EDIT_HIDEDELAY_MIN), minText2, 32);
            int newHideDelayMinutes = _wtoi(minText2);
            
            wchar_t secText2[32];
            GetWindowTextW(GetDlgItem(hDlg, IDC_EDIT_HIDEDELAY_SEC), secText2, 32);
            int newHideDelaySeconds = _wtoi(secText2);
            
            wchar_t fontSizeText2[32];
            GetWindowTextW(GetDlgItem(hDlg, IDC_EDIT_FONTSIZE), fontSizeText2, 32);
            int newFontSize2 = _wtoi(fontSizeText2);
            
            bool newBoldFont2 = (IsDlgButtonChecked(hDlg, IDC_CHECK_BOLD) == BST_CHECKED);
            
            // 检查是否需要重建字体
            if (g_fontSize != newFontSize2 || g_boldFont != newBoldFont2 || g_showYear != newShowYear) {
                if (g_hFont != nullptr) {
                    DeleteObject(g_hFont);
                    g_hFont = nullptr;
                }
            }
            
            // 更新全局变量
            g_use24Hour = newUse24Hour;
            g_showDate = newShowDate;
            g_showSeconds = newShowSeconds;
            g_showYear = newShowYear;
            g_theme = newTheme;
            g_windowSize = newWindowSize;
            g_position = newPosition;
            g_autoHide = newAutoHide;
            g_hideDelayMinutes = newHideDelayMinutes;
            g_hideDelaySeconds = newHideDelaySeconds;
            g_fontSize = newFontSize2;
            g_boldFont = newBoldFont2;
            
            SaveSettings();
            
            if (oldAutoHide2 && !g_autoHide)
            {
                KillTimer(g_hwnd, 2);
                KillTimer(g_hwnd, 4);
                g_isHidden = false;
                g_isHovering = false;
                g_isAnimating = false;
                g_waitingForHide = false;
                g_isHiding = false;
                // 重置动画位置
                RECT workArea;
                SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
                g_animationY = workArea.top + 10;
            }

            if (!oldAutoHide2 && g_autoHide)
            {
                g_isHidden = false;
                g_isHovering = false;
                g_isAnimating = false;
                g_waitingForHide = false;
                g_isHiding = false;
                // 重置动画位置
                RECT workArea;
                SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
                g_animationY = workArea.top + 10;
                UpdateWindowPosition(false);
            }

            UpdateWindowPosition(g_isHovering);
            InvalidateRect(g_hwnd, NULL, TRUE);

            return TRUE;
        }
        break;
        }  // 关闭 switch(LOWORD(wParam))
        break;

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

void ShowSettingsDialog()
{
    HRSRC hRsrc = FindResourceW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_SETTINGS_DIALOG), (LPCWSTR)RT_DIALOG);
    if (!hRsrc)
    {
        MessageBoxW(g_hwnd, L"Dialog resource not found!", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    INT_PTR result = DialogBoxParamW(
        GetModuleHandle(NULL),
        MAKEINTRESOURCEW(IDD_SETTINGS_DIALOG),
        g_hwnd,
        SettingsDlgProc,
        0);

    if (result == -1)
    {
        DWORD error = GetLastError();
        wchar_t msg[256];
        swprintf_s(msg, 256, L"DialogBoxParamW failed! Error: %d", error);
        MessageBoxW(g_hwnd, msg, L"Error", MB_OK | MB_ICONERROR);
    }
}
