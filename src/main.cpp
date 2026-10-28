#include <windows.h>
#include <gdiplus.h>

#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "actions.h"
#include "animation.h"
#include "app_state.h"
#include "island.h"
#include "layout.h"
#include "rendering.h"
#include "resource.h"
#include "settings.h"
#include "settings_dialog.h"
#include "theme.h"
#include "time_format.h"
#include "tray.h"
#include "version.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")

namespace
{
bool s_hovering = false;
bool s_dragging = false;
bool s_dragMoved = false;
POINT s_dragStartCursor = {};
RECT s_dragStartCapsule = {};
bool s_hiddenByFullscreen = false;
ULONGLONG s_lastInsideTick = 0;
int s_contentTickCount = 0;
UINT s_taskbarCreatedMessage = 0;

void ApplyDpiAwareness()
{
    // Per-Monitor V2 awareness keeps the capsule crisp on every monitor.
    typedef BOOL(WINAPI * SetProcessDpiAwarenessContextFn)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr)
        return;

    auto setContext = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setContext != nullptr)
    {
        // (HANDLE)-4 == DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        if (setContext(reinterpret_cast<HANDLE>(-4)))
            return;
    }

    typedef BOOL(WINAPI * SetProcessDPIAwareFn)();
    auto setLegacy = reinterpret_cast<SetProcessDPIAwareFn>(
        GetProcAddress(user32, "SetProcessDPIAware"));
    if (setLegacy != nullptr)
        setLegacy();
}

void SaveSettingsQuietly()
{
    SaveSettings();
}

bool s_autoHideTimerRunning = false;

// The 100 ms auto-hide monitor is only needed while auto-hide or fullscreen
// hiding is enabled, which keeps the idle CPU footprint at the timer tick of
// the clock itself.
void UpdateAutoHideTimerState()
{
    const bool needed = g_settings.autoHide || g_settings.hideOnFullscreen;
    if (needed && !s_autoHideTimerRunning)
    {
        s_lastInsideTick = GetTickCount64();
        SetTimer(g_hwnd, kTimerAutoHide, kAutoHideCheckIntervalMs, nullptr);
        s_autoHideTimerRunning = true;
    }
    else if (!needed && s_autoHideTimerRunning)
    {
        KillTimer(g_hwnd, kTimerAutoHide);
        s_autoHideTimerRunning = false;
    }
}

void RevealIsland()
{
    if (IsIslandHiddenTarget())
    {
        s_hiddenByFullscreen = false;
        SetIslandHiddenTarget(false);
    }
}

void UpdateAutoHide()
{
    const ULONGLONG now = GetTickCount64();
    POINT cursor = {};
    GetCursorPos(&cursor);

    if (g_settings.hideOnFullscreen)
    {
        const bool fullscreen = IsFullscreenAppActive();
        if (fullscreen && !IsIslandHiddenTarget())
        {
            s_hiddenByFullscreen = true;
            s_hovering = false;
            SetIslandHoverTarget(false);
            SetIslandHiddenTarget(true);
        }
        else if (!fullscreen && s_hiddenByFullscreen)
        {
            s_hiddenByFullscreen = false;
            SetIslandHiddenTarget(false);
        }
    }
    else if (s_hiddenByFullscreen)
    {
        s_hiddenByFullscreen = false;
        if (IsIslandHiddenTarget())
            SetIslandHiddenTarget(false);
    }

    if (!g_settings.autoHide)
        return;

    const RECT trigger = GetIslandTriggerRect();
    const bool inside = PointInRect(trigger, cursor);
    if (inside)
        s_lastInsideTick = now;

    if (IsIslandHiddenTarget())
    {
        if (inside && !s_hiddenByFullscreen)
        {
            SetIslandHiddenTarget(false);
            s_hovering = true;
            SetIslandHoverTarget(true);
            s_lastInsideTick = now;
        }
        return;
    }

    // Keep the capsule visible while the user is dragging it or configuring it.
    if (s_dragging || IsSettingsDialogOpen())
    {
        s_lastInsideTick = now;
        return;
    }

    // The cursor position is authoritative (WM_MOUSELEAVE is not delivered when
    // the window slides under a stationary pointer), so collapse the hover state
    // as soon as the pointer leaves the trigger area.
    if (!inside)
    {
        if (s_hovering)
        {
            s_hovering = false;
            SetIslandHoverTarget(false);
        }

        const ULONGLONG delay =
            static_cast<ULONGLONG>(std::max(0, g_settings.hideDelaySeconds)) * 1000ULL;
        if (now - s_lastInsideTick >= delay)
            SetIslandHiddenTarget(true);
    }
}

void BeginDrag(POINT cursor)
{
    s_dragging = true;
    s_dragMoved = false;
    s_dragStartCursor = cursor;
    s_dragStartCapsule = GetIslandRestingCapsuleRect();
    SetCapture(g_hwnd);
}

void UpdateDrag(POINT cursor)
{
    if (!s_dragging)
        return;

    const int deltaX = cursor.x - s_dragStartCursor.x;
    const int deltaY = cursor.y - s_dragStartCursor.y;
    if (!s_dragMoved && std::abs(deltaX) < 4 && std::abs(deltaY) < 4)
        return;
    s_dragMoved = true;

    const int width = s_dragStartCapsule.right - s_dragStartCapsule.left;
    const int height = s_dragStartCapsule.bottom - s_dragStartCapsule.top;

    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    RECT workArea = GetIslandWorkArea();
    if (GetMonitorInfoW(monitor, &info))
        workArea = info.rcWork;

    const int minLeft = static_cast<int>(workArea.left);
    const int minTop = static_cast<int>(workArea.top);
    const int maxLeft = std::max(minLeft, static_cast<int>(workArea.right) - width);
    const int maxTop = std::max(minTop, static_cast<int>(workArea.bottom) - height);
    int left = s_dragStartCapsule.left + deltaX;
    int top = s_dragStartCapsule.top + deltaY;
    left = std::clamp(left, minLeft, maxLeft);
    top = std::clamp(top, minTop, maxTop);

    g_settings.position = ISLAND_POS_CUSTOM;
    g_settings.customX = left;
    g_settings.customY = top;

    RenderIslandFrame(true);
}

void EndDrag(bool commit)
{
    if (!s_dragging)
        return;

    s_dragging = false;
    if (GetCapture() == g_hwnd)
        ReleaseCapture();

    if (commit && s_dragMoved)
    {
        SaveSettingsQuietly();
        UpdateTrayIcon();
        RefreshSettingsDialog();
    }
    else if (!commit)
    {
        // Restore the capsule to its previous location (usually a click).
        g_settings.customX = s_dragStartCapsule.left;
        g_settings.customY = s_dragStartCapsule.top;
        if (s_dragStartCapsule.left == -100000)
        {
            g_settings.customX = -100000;
            g_settings.customY = -100000;
        }
        RenderIslandFrame(true);
    }
}

void ShowIslandSettings()
{
    RevealIsland();
    ShowSettingsDialog(g_hwnd);
}

void HandleCommand(WORD command)
{
    switch (command)
    {
    case TRAY_CMD_SETTINGS:
        ShowIslandSettings();
        break;
    case TRAY_CMD_TIME_SETTINGS:
        OpenDateAndTimeSettings();
        break;
    case TRAY_CMD_AUTOSTART:
    {
        const bool enabled = !IsAutoStartEnabled();
        SetAutoStartEnabled(enabled);
        g_settings.autoStart = enabled;
        SaveSettings();
        UpdateTrayIcon();
        RefreshSettingsDialog();
        break;
    }
    case TRAY_CMD_RESET_POSITION:
        ResetIslandPosition();
        break;
    case TRAY_CMD_ABOUT:
        ShowAboutDialog(g_hwnd);
        break;
    case TRAY_CMD_EXIT:
        RequestApplicationExit();
        break;
    default:
        break;
    }
}

void RefreshFromSystemTheme()
{
    RefreshSystemThemeCache();
    InvalidateIslandContent();
    RenderIslandFrame(true);
    UpdateTrayIcon();
    RefreshSettingsDialog();
}

LRESULT CALLBACK IslandWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
    {
        // The layered window is painted through UpdateLayeredWindow.
        ValidateRect(hwnd, nullptr);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_TIMER:
        switch (wParam)
        {
        case kTimerContent:
            ++s_contentTickCount;
            if ((s_contentTickCount % 25) == 0)
            {
                // Poll the system theme once every five seconds as a fallback
                // for systems that do not broadcast a theme change message.
                RefreshSystemThemeCache();
            }
            // The renderer compares text/theme/geometry itself and only repaints
            // when something actually changed.
            RenderIslandFrame(false);
            return 0;
        case kTimerAutoHide:
            UpdateAutoHide();
            return 0;
        case kTimerAnimation:
            UpdateIslandAnimation();
            return 0;
        case kTimerTray:
            UpdateTrayIcon();
            return 0;
        default:
            return 0;
        }

    case WM_MOUSEMOVE:
    {
        POINT cursor = {};
        GetCursorPos(&cursor);
        if (s_dragging)
        {
            UpdateDrag(cursor);
            return 0;
        }

        if (!s_hovering)
        {
            s_hovering = true;
            SetIslandHoverTarget(true);
            TRACKMOUSEEVENT track = {};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;
            TrackMouseEvent(&track);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        s_hovering = false;
        SetIslandHoverTarget(false);
        s_lastInsideTick = GetTickCount64();
        return 0;

    case WM_LBUTTONDOWN:
    {
        POINT cursor = {};
        GetCursorPos(&cursor);
        BeginDrag(cursor);
        return 0;
    }

    case WM_LBUTTONUP:
    {
        const bool wasMoved = s_dragMoved;
        EndDrag(true);
        if (!wasMoved)
            OpenDateAndTimeSettings();
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (s_dragging)
            EndDrag(false);
        return 0;

    case WM_RBUTTONDOWN:
        ShowIslandSettings();
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            SetCursor(LoadCursorW(nullptr, s_dragging ? IDC_SIZEALL : IDC_HAND));
            return TRUE;
        }
        break;

    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        InitIslandLayout(hwnd);
        FinishIslandAnimation();
        return 0;

    case WM_SETTINGCHANGE:
        if (wParam == SPI_SETWORKAREA)
        {
            InitIslandLayout(hwnd);
            FinishIslandAnimation();
        }
        else if (lParam != 0 &&
                 wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0)
        {
            RefreshFromSystemTheme();
        }
        return 0;

    case WM_THEMECHANGED:
        RefreshFromSystemTheme();
        return 0;

    case WM_POWERBROADCAST:
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
        {
            RefreshFromSystemTheme();
        }
        return TRUE;

    case WM_ENDSESSION:
        if (wParam != FALSE)
            SaveSettings();
        return 0;

    case WM_COMMAND:
        if (HIWORD(wParam) == 0)
            HandleCommand(LOWORD(wParam));
        return 0;

    case kMsgShowSettings:
        ShowIslandSettings();
        return 0;

    case kMsgApplySettings:
        ApplySettingsAndRefresh();
        return 0;

    case kMsgRefreshTheme:
        RefreshFromSystemTheme();
        return 0;

    case kMsgRequestExit:
        DestroyWindow(hwnd);
        return 0;

    case kTrayCallbackMessage:
        HandleTrayCallback(wParam, lParam);
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, kTimerContent);
        KillTimer(hwnd, kTimerAutoHide);
        KillTimer(hwnd, kTimerTray);
        StopIslandAnimationTimer();
        CloseSettingsDialog(false);
        DestroyTrayIcon();
        ShutdownIslandRenderer();
        PostQuitMessage(0);
        return 0;

    default:
        if (s_taskbarCreatedMessage != 0 && message == s_taskbarCreatedMessage)
        {
            ReinstallTrayIcon();
            return 0;
        }
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace

// ---------------------------------------------------------------------------
// Application level commands (declared in island.h)
// ---------------------------------------------------------------------------
void ApplySettingsAndRefresh()
{
    ValidateSettings(g_settings);

    if (!g_settings.autoHide && !g_settings.hideOnFullscreen)
    {
        s_hiddenByFullscreen = false;
        if (IsIslandHiddenTarget())
        {
            SetIslandHiddenTarget(false);
            SetIslandHoverTarget(s_hovering);
        }
    }

    UpdateAutoHideTimerState();
    InitIslandLayout(g_hwnd);
    FinishIslandAnimation(); // re-render + reposition immediately
    UpdateTrayIcon();
    RefreshSettingsDialog();
}

void ResetIslandPosition()
{
    ClearCustomPosition();
    SaveSettings();
    ApplySettingsAndRefresh();
}

void RequestApplicationExit()
{
    if (g_hwnd != nullptr)
        PostMessageW(g_hwnd, kMsgRequestExit, 0, 0);
}

void RefreshIslandFromSystemTheme()
{
    RefreshFromSystemTheme();
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    ApplyDpiAwareness();

    const HRESULT comResult =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kIslandMutexName);
    if (mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND existing = FindWindowW(kIslandWindowClass, nullptr);
        if (existing != nullptr)
            PostMessageW(existing, kMsgShowSettings, 0, 0);
        if (mutex != nullptr)
            CloseHandle(mutex);
        if (SUCCEEDED(comResult))
            CoUninitialize();
        return 0;
    }

    Gdiplus::GdiplusStartupInput startupInput;
    ULONG_PTR gdiplusToken = 0;
    const Gdiplus::Status gdiplusStatus =
        Gdiplus::GdiplusStartup(&gdiplusToken, &startupInput, nullptr);

    LoadSettings();

    // Keep the Run entry pointing at this executable (the path may have moved
    // since the user enabled auto-start).
    if (g_settings.autoStart)
        SetAutoStartEnabled(true);

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = IslandWndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kIslandWindowClass;
    windowClass.hIcon = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 32, 32,
        LR_DEFAULTCOLOR));
    windowClass.hIconSm = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16,
        LR_DEFAULTCOLOR));

    if (!RegisterClassExW(&windowClass))
    {
        MessageBoxW(nullptr, L"Failed to register the island window class.",
                    APP_NAME, MB_OK | MB_ICONERROR);
        if (SUCCEEDED(comResult))
            CoUninitialize();
        return 1;
    }

    g_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kIslandWindowClass, APP_NAME, WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
        instance, nullptr);
    if (g_hwnd == nullptr)
    {
        MessageBoxW(nullptr, L"Failed to create the island window.", APP_NAME,
                    MB_OK | MB_ICONERROR);
        if (SUCCEEDED(comResult))
            CoUninitialize();
        return 1;
    }

    InitIslandLayout(g_hwnd);
    InitIslandAnimation(g_hwnd);
    InitIslandRenderer(g_hwnd);

    s_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    s_lastInsideTick = GetTickCount64();

    SetTimer(g_hwnd, kTimerContent, kContentCheckIntervalMs, nullptr);
    UpdateAutoHideTimerState();
    SetTimer(g_hwnd, kTimerTray, kTooltipRefreshIntervalMs, nullptr);

    RenderIslandFrame(true);
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    InitTrayIcon(g_hwnd);

    if (wcsstr(GetCommandLineW(), L"--settings") != nullptr)
        PostMessageW(g_hwnd, kMsgShowSettings, 0, 0);

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (gdiplusStatus == Gdiplus::Ok)
        Gdiplus::GdiplusShutdown(gdiplusToken);
    if (SUCCEEDED(comResult))
        CoUninitialize();
    if (mutex != nullptr)
        CloseHandle(mutex);

    return static_cast<int>(message.wParam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    return wWinMain(instance, nullptr, GetCommandLineW(), SW_SHOWNORMAL);
}
