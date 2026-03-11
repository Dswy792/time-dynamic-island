#include "settings.h"

#include <windows.h>
#include <cstdlib>
#include <stdio.h>

#include "app_state.h"
#include "layout.h"
#include "resource.h"

void LoadSettings()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\DynamicIsland", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD size;
        size = sizeof(g_use24Hour);
        RegQueryValueExW(hKey, L"Use24Hour", NULL, NULL, reinterpret_cast<LPBYTE>(&g_use24Hour), &size);
        size = sizeof(g_showDate);
        RegQueryValueExW(hKey, L"ShowDate", NULL, NULL, reinterpret_cast<LPBYTE>(&g_showDate), &size);
        size = sizeof(g_showYear);
        RegQueryValueExW(hKey, L"ShowYear", NULL, NULL, reinterpret_cast<LPBYTE>(&g_showYear), &size);
        size = sizeof(g_showSeconds);
        RegQueryValueExW(hKey, L"ShowSeconds", NULL, NULL, reinterpret_cast<LPBYTE>(&g_showSeconds), &size);
        size = sizeof(g_theme);
        RegQueryValueExW(hKey, L"Theme", NULL, NULL, reinterpret_cast<LPBYTE>(&g_theme), &size);
        size = sizeof(g_autoHide);
        RegQueryValueExW(hKey, L"AutoHide", NULL, NULL, reinterpret_cast<LPBYTE>(&g_autoHide), &size);
        size = sizeof(g_windowSize);
        RegQueryValueExW(hKey, L"WindowSize", NULL, NULL, reinterpret_cast<LPBYTE>(&g_windowSize), &size);
        size = sizeof(g_position);
        RegQueryValueExW(hKey, L"Position", NULL, NULL, reinterpret_cast<LPBYTE>(&g_position), &size);
        size = sizeof(g_hideDelayMinutes);
        RegQueryValueExW(hKey, L"HideDelayMinutes", NULL, NULL, reinterpret_cast<LPBYTE>(&g_hideDelayMinutes), &size);
        size = sizeof(g_hideDelaySeconds);
        RegQueryValueExW(hKey, L"HideDelaySeconds", NULL, NULL, reinterpret_cast<LPBYTE>(&g_hideDelaySeconds), &size);
        size = sizeof(g_fontSize);
        RegQueryValueExW(hKey, L"FontSize", NULL, NULL, reinterpret_cast<LPBYTE>(&g_fontSize), &size);
        size = sizeof(g_boldFont);
        RegQueryValueExW(hKey, L"BoldFont", NULL, NULL, reinterpret_cast<LPBYTE>(&g_boldFont), &size);
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
        switch (LOWORD(wParam))
        {
        case IDOK:
            g_use24Hour = IsDlgButtonChecked(hDlg, IDC_RADIO_24HOUR) == BST_CHECKED;
            g_showDate = IsDlgButtonChecked(hDlg, IDC_CHECK_DATE) == BST_CHECKED;
            g_showSeconds = IsDlgButtonChecked(hDlg, IDC_CHECK_SECONDS) == BST_CHECKED;
            g_showYear = IsDlgButtonChecked(hDlg, IDC_CHECK_YEAR) == BST_CHECKED;
            g_theme = IsDlgButtonChecked(hDlg, IDC_RADIO_DARK) == BST_CHECKED ? 0 : (IsDlgButtonChecked(hDlg, IDC_RADIO_LIGHT) == BST_CHECKED ? 1 : 2);

            g_windowSize = IsDlgButtonChecked(hDlg, IDC_RADIO_SMALL) == BST_CHECKED ? 0 : (IsDlgButtonChecked(hDlg, IDC_RADIO_MEDIUM) == BST_CHECKED ? 1 : 2);

            g_position = IsDlgButtonChecked(hDlg, IDC_RADIO_LEFT) == BST_CHECKED ? 0 : (IsDlgButtonChecked(hDlg, IDC_RADIO_CENTER) == BST_CHECKED ? 1 : 2);

            g_autoHide = IsDlgButtonChecked(hDlg, IDC_CHECK_AUTOHIDE) == BST_CHECKED;

            {
                wchar_t minText[32];
                GetWindowTextW(GetDlgItem(hDlg, IDC_EDIT_HIDEDELAY_MIN), minText, 32);
                g_hideDelayMinutes = _wtoi(minText);

                wchar_t secText[32];
                GetWindowTextW(GetDlgItem(hDlg, IDC_EDIT_HIDEDELAY_SEC), secText, 32);
                g_hideDelaySeconds = _wtoi(secText);
            }

            wchar_t fontSizeText[32];
            GetWindowTextW(GetDlgItem(hDlg, IDC_EDIT_FONTSIZE), fontSizeText, 32);
            g_fontSize = _wtoi(fontSizeText);

            g_boldFont = IsDlgButtonChecked(hDlg, IDC_CHECK_BOLD) == BST_CHECKED;

            SaveSettings();

            UpdateWindowPosition(g_isHovering);
            InvalidateRect(g_hwnd, NULL, TRUE);
            EndDialog(hDlg, IDOK);
            return TRUE;

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
            g_use24Hour = IsDlgButtonChecked(hDlg, IDC_RADIO_24HOUR) == BST_CHECKED;
            g_showDate = IsDlgButtonChecked(hDlg, IDC_CHECK_DATE) == BST_CHECKED;
            g_showSeconds = IsDlgButtonChecked(hDlg, IDC_CHECK_SECONDS) == BST_CHECKED;
            g_showYear = IsDlgButtonChecked(hDlg, IDC_CHECK_YEAR) == BST_CHECKED;
            g_theme = IsDlgButtonChecked(hDlg, IDC_RADIO_DARK) == BST_CHECKED ? 0 : (IsDlgButtonChecked(hDlg, IDC_RADIO_LIGHT) == BST_CHECKED ? 1 : 2);

            g_windowSize = IsDlgButtonChecked(hDlg, IDC_RADIO_SMALL) == BST_CHECKED ? 0 : (IsDlgButtonChecked(hDlg, IDC_RADIO_MEDIUM) == BST_CHECKED ? 1 : 2);

            g_position = IsDlgButtonChecked(hDlg, IDC_RADIO_LEFT) == BST_CHECKED ? 0 : (IsDlgButtonChecked(hDlg, IDC_RADIO_CENTER) == BST_CHECKED ? 1 : 2);

            g_autoHide = IsDlgButtonChecked(hDlg, IDC_CHECK_AUTOHIDE) == BST_CHECKED;

            {
                wchar_t minText2[32];
                GetWindowTextW(GetDlgItem(hDlg, IDC_EDIT_HIDEDELAY_MIN), minText2, 32);
                g_hideDelayMinutes = _wtoi(minText2);

                wchar_t secText2[32];
                GetWindowTextW(GetDlgItem(hDlg, IDC_EDIT_HIDEDELAY_SEC), secText2, 32);
                g_hideDelaySeconds = _wtoi(secText2);
            }

            wchar_t fontSizeText2[32];
            GetWindowTextW(GetDlgItem(hDlg, IDC_EDIT_FONTSIZE), fontSizeText2, 32);
            g_fontSize = _wtoi(fontSizeText2);

            g_boldFont = IsDlgButtonChecked(hDlg, IDC_CHECK_BOLD) == BST_CHECKED;

            SaveSettings();

            UpdateWindowPosition(g_isHovering);
            InvalidateRect(g_hwnd, NULL, TRUE);

            MessageBoxW(hDlg, L"Settings applied successfully!", L"Applied", MB_OK | MB_ICONINFORMATION);
            return TRUE;
        }
        return TRUE;

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
