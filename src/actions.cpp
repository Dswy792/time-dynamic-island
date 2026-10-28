#include "actions.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windows.h>

#include <string>

#include "app_state.h"
#include "version.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")

void OpenDateAndTimeSettings()
{
    ShellExecuteW(nullptr, L"open", L"control.exe", L"timedate.cpl", nullptr,
                  SW_SHOWNORMAL);
}

void ShowAboutDialog(HWND owner)
{
    std::wstring text;
    text += APP_NAME;
    text += L" ";
    text += APP_VERSION_STRING;
    text += L"\n\n";
    text += L"一个优雅的 Windows 顶部时间胶囊。\n";
    text += L"A minimalist clock capsule for the desktop.\n\n";
    text += L"左键点击：打开系统日期和时间设置\n";
    text += L"右键点击：打开灵动岛设置\n";
    text += L"拖动：移动灵动岛窗口\n";
    text += L"托盘图标：更多选项\n\n";
    text += L"纯 Win32 + GDI+ 实现，不使用钩子、不注入进程。";
    MessageBoxW(owner, text.c_str(), L"About Dynamic Island",
                MB_OK | MB_ICONINFORMATION);
}

bool IsFullscreenAppActive()
{
    HWND foreground = GetForegroundWindow();
    if (foreground == nullptr || foreground == g_hwnd)
        return false;

    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    if (processId == GetCurrentProcessId())
        return false;

    if (IsIconic(foreground) || !IsWindowVisible(foreground))
        return false;

    wchar_t className[256] = {};
    if (GetClassNameW(foreground, className, 256) > 0)
    {
        if (wcscmp(className, L"Progman") == 0 ||
            wcscmp(className, L"WorkerW") == 0 ||
            wcscmp(className, L"Shell_TrayWnd") == 0 ||
            wcscmp(className, L"Shell_SecondaryTrayWnd") == 0 ||
            wcscmp(className, L"Windows.UI.Core.CoreWindow") == 0)
            return false;
    }

    // Cloaked windows belong to suspended UWP apps / other desktops.
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(foreground, DWMWA_CLOAKED, &cloaked,
                                        sizeof(cloaked))) &&
        cloaked)
        return false;

    RECT windowRect = {};
    if (!GetWindowRect(foreground, &windowRect))
        return false;

    HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo))
        return false;

    const RECT& screen = monitorInfo.rcMonitor;
    constexpr LONG tolerance = 1;
    return windowRect.left <= screen.left + tolerance &&
           windowRect.top <= screen.top + tolerance &&
           windowRect.right >= screen.right - tolerance &&
           windowRect.bottom >= screen.bottom - tolerance;
}
