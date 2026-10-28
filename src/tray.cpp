#include "tray.h"

#include <windows.h>
#include <shellapi.h>

#include <string>

#include "actions.h"
#include "app_state.h"
#include "island.h"
#include "resource.h"
#include "settings.h"
#include "settings_dialog.h"
#include "time_format.h"

#pragma comment(lib, "shell32.lib")

namespace
{
constexpr UINT kTrayIconId = 1;

HWND s_hwnd = nullptr;
HICON s_icon = nullptr;
bool s_installed = false;
UINT s_taskbarCreatedMessage = 0;

HICON LoadTrayIcon()
{
    const int size = GetSystemMetrics(SM_CXSMICON);
    HICON icon = static_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, size,
        size, LR_DEFAULTCOLOR));
    if (icon == nullptr)
    {
        icon = static_cast<HICON>(LoadImageW(
            GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16,
            16, LR_DEFAULTCOLOR));
    }
    if (icon == nullptr)
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    return icon;
}

void FillNotifyIconData(NOTIFYICONDATAW& data)
{
    data = {};
    data.cbSize = sizeof(NOTIFYICONDATAW);
    data.hWnd = s_hwnd;
    data.uID = kTrayIconId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = kTrayCallbackMessage;
    data.hIcon = s_icon;

    const std::wstring tooltip = GetTooltipText();
    wcsncpy_s(data.szTip, tooltip.c_str(), _TRUNCATE);
}

void ShowContextMenu()
{
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
        return;

    AppendMenuW(menu, MF_STRING, TRAY_CMD_SETTINGS, L"设置(灵动岛)...");
    SetMenuDefaultItem(menu, TRAY_CMD_SETTINGS, FALSE);
    AppendMenuW(menu, MF_STRING, TRAY_CMD_TIME_SETTINGS, L"日期和时间设置");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    UINT autoStartFlags = MF_STRING;
    if (IsAutoStartEnabled())
        autoStartFlags |= MF_CHECKED;
    AppendMenuW(menu, autoStartFlags, TRAY_CMD_AUTOSTART, L"开机自启动");
    AppendMenuW(menu, MF_STRING, TRAY_CMD_RESET_POSITION, L"重置位置");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, TRAY_CMD_ABOUT, L"关于");
    AppendMenuW(menu, MF_STRING, TRAY_CMD_EXIT, L"退出");

    POINT cursor = {};
    GetCursorPos(&cursor);

    SetForegroundWindow(s_hwnd);
    const UINT command = TrackPopupMenuEx(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, cursor.x, cursor.y,
        s_hwnd, nullptr);
    DestroyMenu(menu);
    PostMessageW(s_hwnd, WM_NULL, 0, 0);

    if (command != 0)
        PostMessageW(s_hwnd, WM_COMMAND, MAKEWPARAM(command, 0), 0);
}
} // namespace

void InitTrayIcon(HWND hwnd)
{
    s_hwnd = hwnd;
    if (s_icon == nullptr)
        s_icon = LoadTrayIcon();

    if (s_taskbarCreatedMessage == 0)
        s_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    if (s_installed)
    {
        NOTIFYICONDATAW removeData = {};
        removeData.cbSize = sizeof(NOTIFYICONDATAW);
        removeData.hWnd = s_hwnd;
        removeData.uID = kTrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &removeData);
        s_installed = false;
    }

    NOTIFYICONDATAW data;
    FillNotifyIconData(data);
    s_installed = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    if (s_installed)
    {
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }
}

void DestroyTrayIcon()
{
    if (!s_installed)
        return;

    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(NOTIFYICONDATAW);
    data.hWnd = s_hwnd;
    data.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &data);
    s_installed = false;

    if (s_icon != nullptr)
    {
        DestroyIcon(s_icon);
        s_icon = nullptr;
    }
}

void ReinstallTrayIcon()
{
    s_installed = false;
    InitTrayIcon(s_hwnd);
}

void UpdateTrayIcon()
{
    if (!s_installed)
        return;

    NOTIFYICONDATAW data;
    FillNotifyIconData(data);
    data.uFlags = NIF_TIP | NIF_SHOWTIP;
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void HandleTrayCallback(WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);

    const UINT event = LOWORD(lParam);
    switch (event)
    {
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        PostMessageW(s_hwnd, kMsgShowSettings, 0, 0);
        break;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        ShowContextMenu();
        break;
    default:
        break;
    }
}
