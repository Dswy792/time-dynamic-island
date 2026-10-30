#include <windows.h>
#include <gdiplus.h>

#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

#include "actions.h"
#include "animation.h"
#include "app_state.h"
#include "chat_watcher.h"
#include "island.h"
#include "layout.h"
#include "notifications.h"
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

// Message sub-islands: at most one circle per source, each keeps its own dwell
// deadline. While the mouse hovers a circle its deadline is extended so the
// tooltip stays readable.
ULONGLONG s_subDeadline[kNotificationSourceCount] = {0, 0};
bool s_subDismissTimerRunning = false;
int s_pressedSub = -1;
int s_hoveredSub = -1;
bool s_lastNotifyEnable = false;

void ApplyDpiAwareness()
{
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

IslandClusterGeometry CurrentClusterGeometry()
{
    float progress[kNotificationSourceCount] = {GetSubIslandProgress(0),
                                                GetSubIslandProgress(1)};
    return ComputeIslandClusterGeometry(GetIslandHoverProgress(),
                                        GetIslandHideProgress(), progress,
                                        s_hoveredSub, s_hoveredSub >= 0);
}

int HitTestSubIsland(POINT screenPoint)
{
    if (!IsAnySubIslandVisible())
        return -1;
    const IslandClusterGeometry geometry = CurrentClusterGeometry();
    float progress[kNotificationSourceCount] = {GetSubIslandProgress(0),
                                                GetSubIslandProgress(1)};
    for (int i = 0; i < kNotificationSourceCount; ++i)
    {
        if (progress[i] < 0.35f)
            continue;
        if (PointInRect(geometry.subIslands[i], screenPoint))
            return i;
    }
    return -1;
}

void SetHoveredSub(int index)
{
    if (s_hoveredSub == index)
        return;
    s_hoveredSub = index;
    SetHoveredSubIsland(index);
    InvalidateIslandContent();
    RenderIslandFrame(true);
}

void EndDrag(bool commit);
void ShowIslandSettings();

void OnLButtonDownOnIsland(POINT cursor)
{
    const int sub = HitTestSubIsland(cursor);
    if (sub >= 0)
    {
        s_pressedSub = sub;
        SetCapture(g_hwnd);
        return;
    }

    const IslandClusterGeometry geometry = CurrentClusterGeometry();
    if (PointInRect(geometry.capsule, cursor))
    {
        s_dragging = true;
        s_dragMoved = false;
        s_dragStartCursor = cursor;
        s_dragStartCapsule = GetIslandRestingCapsuleRect();
        SetCapture(g_hwnd);
    }
}

void OnLButtonUpOnIsland(POINT cursor)
{
    if (s_pressedSub >= 0)
    {
        const int sub = s_pressedSub;
        s_pressedSub = -1;
        if (GetCapture() == g_hwnd)
            ReleaseCapture();
        if (HitTestSubIsland(cursor) == sub)
        {
            ActivateSourceApp(sub == 1 ? NotificationSource::QQ
                                       : NotificationSource::WeChat);
            DismissNotificationSubIsland(sub == 1 ? NotificationSource::QQ
                                                  : NotificationSource::WeChat);
        }
        return;
    }

    const bool wasMoved = s_dragMoved;
    EndDrag(true);
    if (!wasMoved)
        ShowIslandSettings();
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

void UpdateAutoHide()
{
    const ULONGLONG now = GetTickCount64();
    POINT cursor = {};
    GetCursorPos(&cursor);

    // Keep the island on screen while message circles are visible.
    if (IsAnySubIslandVisible())
    {
        s_lastInsideTick = now;
        return;
    }

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

    const RECT trigger = GetIslandClusterTriggerRect();
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

    if (s_dragging || IsSettingsDialogOpen())
    {
        s_lastInsideTick = now;
        return;
    }

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

// The sub-island is persistent by default (deadline 0): it stays on screen
// until the user clicks it or hides notifications. Only an explicit 1..10 s
// "停留时长" arms the auto-collapse timer.
void RefreshSubDismissTimer()
{
    bool anyTimed = false;
    for (int i = 0; i < kNotificationSourceCount; ++i)
    {
        if (IsSubIslandTarget(i) && s_subDeadline[i] > 0)
        {
            anyTimed = true;
            break;
        }
    }

    if (anyTimed && !s_subDismissTimerRunning)
    {
        SetTimer(g_hwnd, kTimerNotificationDismiss, 250, nullptr);
        s_subDismissTimerRunning = true;
    }
    else if (!anyTimed && s_subDismissTimerRunning)
    {
        KillTimer(g_hwnd, kTimerNotificationDismiss);
        s_subDismissTimerRunning = false;
    }
}

void CheckSubIslandDeadlines()
{
    const ULONGLONG now = GetTickCount64();
    for (int i = 0; i < kNotificationSourceCount; ++i)
    {
        if (!IsSubIslandTarget(i) || s_subDeadline[i] == 0)
            continue; // persistent badge
        if (s_hoveredSub == i)
        {
            // Keep the hovered circle (and its tooltip) alive a bit longer.
            s_subDeadline[i] = now + 2000;
            continue;
        }
        if (now >= s_subDeadline[i])
        {
            s_subDeadline[i] = 0;
            SetSubIslandTarget(i, false);
            if (s_hoveredSub == i)
                SetHoveredSub(-1);
            InvalidateIslandContent();
            RenderIslandFrame(true);
        }
    }
    RefreshSubDismissTimer();
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

// ---------------------------------------------------------------------------
// Messaging app activation
// ---------------------------------------------------------------------------
bool ProcessExeMatches(DWORD processId, const wchar_t* const* names, int count)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                 processId);
    if (process == nullptr)
        return false;

    wchar_t path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    const BOOL ok = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    if (!ok)
        return false;

    const wchar_t* base = wcsrchr(path, L'\\');
    base = (base != nullptr) ? base + 1 : path;
    for (int index = 0; index < count; ++index)
    {
        if (_wcsicmp(base, names[index]) == 0)
            return true;
    }
    return false;
}

struct WindowSearch
{
    NotificationSource source;
    HWND visibleResult;
    HWND hiddenResult;
};

BOOL CALLBACK FindMessagingWindowProc(HWND hwnd, LPARAM param)
{
    WindowSearch* search = reinterpret_cast<WindowSearch*>(param);
    if (GetWindow(hwnd, GW_OWNER) != nullptr)
        return TRUE;

    RECT rect = {};
    if (!GetWindowRect(hwnd, &rect))
        return TRUE;
    // A minimised Qt/Chromium window reports a tiny taskbar-sized rectangle, so
    // only apply the size filter to windows that are not minimised.
    if (!IsIconic(hwnd) &&
        (rect.right - rect.left < 160 || rect.bottom - rect.top < 120))
        return TRUE;

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == 0)
        return TRUE;

    wchar_t className[256] = {};
    if (GetClassNameW(hwnd, className, 256) == 0)
        return TRUE;

    if (search->source == NotificationSource::WeChat)
    {
        const wchar_t* names[] = {L"Weixin.exe", L"WeChat.exe", L"WeixinApp.exe"};
        if (!ProcessExeMatches(processId, names, 3))
            return TRUE;
        // Only the real main window (Qt51514QWindowIcon), never the tray-icon
        // message helper window.
        if (wcsstr(className, L"QWindowIcon") == nullptr)
            return TRUE;
    }
    else if (search->source == NotificationSource::QQ)
    {
        const wchar_t* names[] = {L"QQ.exe"};
        if (!ProcessExeMatches(processId, names, 1))
            return TRUE;
        if (wcscmp(className, L"Chrome_WidgetWin_1") != 0)
            return TRUE;
    }
    else
    {
        return TRUE;
    }

    if (IsWindowVisible(hwnd))
    {
        search->visibleResult = hwnd;
        return FALSE;
    }

    if (search->hiddenResult == nullptr)
        search->hiddenResult = hwnd;
    return TRUE;
}

// ---------------------------------------------------------------------------
// Notification handling
// ---------------------------------------------------------------------------
void OnAppNotification(const AppNotification& notification)
{
    if (!g_settings.notifyEnable)
        return;
    if (notification.source == NotificationSource::WeChat && !g_settings.notifyWeChat)
        return;
    if (notification.source == NotificationSource::QQ && !g_settings.notifyQQ)
        return;
    if (notification.source == NotificationSource::Other)
        return;

    ShowNotificationSubIsland(notification);
}

LRESULT CALLBACK IslandWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
        ValidateRect(hwnd, nullptr);
        return 0;

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
                RefreshSystemThemeCache();
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
        case kTimerNotificationDismiss:
            CheckSubIslandDeadlines();
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

        const int sub = HitTestSubIsland(cursor);
        if (sub != s_hoveredSub)
            SetHoveredSub(sub);

        bool overMain = false;
        if (sub < 0)
        {
            const IslandClusterGeometry geometry = CurrentClusterGeometry();
            overMain = PointInRect(geometry.capsule, cursor);
        }

        if (overMain && !s_hovering)
        {
            s_hovering = true;
            SetIslandHoverTarget(true);
        }
        else if (!overMain && s_hovering)
        {
            s_hovering = false;
            SetIslandHoverTarget(false);
        }

        if (!s_hovering)
        {
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
        SetHoveredSub(-1);
        s_lastInsideTick = GetTickCount64();
        return 0;

    case WM_LBUTTONDOWN:
    {
        POINT cursor = {};
        GetCursorPos(&cursor);
        OnLButtonDownOnIsland(cursor);
        return 0;
    }

    case WM_LBUTTONUP:
    {
        POINT cursor = {};
        GetCursorPos(&cursor);
        OnLButtonUpOnIsland(cursor);
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (s_dragging)
            EndDrag(false);
        s_pressedSub = -1;
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
            RefreshFromSystemTheme();
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

    case kMsgNotification:
        HandleNotificationMessage(wParam, lParam);
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
        KillTimer(hwnd, kTimerNotificationDismiss);
        StopIslandAnimationTimer();
        ShutdownNotifications();
        ShutdownChatWatcher();
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

    if (!g_settings.notifyEnable)
    {
        for (int i = 0; i < kNotificationSourceCount; ++i)
        {
            s_subDeadline[i] = 0;
            SetSubIslandTarget(i, false);
        }
    }
    else
    {
        if (!g_settings.notifyWeChat)
        {
            s_subDeadline[0] = 0;
            SetSubIslandTarget(0, false);
        }
        if (!g_settings.notifyQQ)
        {
            s_subDeadline[1] = 0;
            SetSubIslandTarget(1, false);
        }
        if (!s_lastNotifyEnable)
            RequestNotificationAccessFromUi();

        const int configured = std::clamp(g_settings.notifyPeekSeconds,
                                          kNotifyPeekSecondsMin,
                                          kNotifyPeekSecondsMax);
        for (int i = 0; i < kNotificationSourceCount; ++i)
        {
            if (!IsSubIslandTarget(i))
                continue;
            s_subDeadline[i] =
                configured > 0
                    ? GetTickCount64() + static_cast<ULONGLONG>(configured) * 1000ULL
                    : 0ULL;
        }
    }
    s_lastNotifyEnable = g_settings.notifyEnable;
    RefreshSubDismissTimer();

    UpdateAutoHideTimerState();
    InitIslandLayout(g_hwnd);
    InvalidateIslandContent();
    FinishIslandAnimation();
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

void ShowNotificationSubIsland(const AppNotification& notification)
{
    const int index = NotificationSourceIndex(notification.source);
    if (index < 0 || index >= kNotificationSourceCount)
        return;

    std::wstring title = notification.title;
    std::wstring body = notification.body;
    if (title.empty() && !body.empty())
    {
        title = body;
        body.clear();
    }
    SetSubIslandContent(index, title, body);
    SetSubIslandTarget(index, true);

    const int configured = std::clamp(g_settings.notifyPeekSeconds,
                                      kNotifyPeekSecondsMin,
                                      kNotifyPeekSecondsMax);
    s_subDeadline[index] =
        configured > 0 ? GetTickCount64() + static_cast<ULONGLONG>(configured) * 1000ULL
                       : 0ULL;
    RefreshSubDismissTimer();

    s_hiddenByFullscreen = false;
    SetIslandHiddenTarget(false);
    SetIslandHoverTarget(false);
    s_hovering = false;
    InvalidateIslandContent();
    RenderIslandFrame(true);
}

void DismissNotificationSubIsland(NotificationSource source)
{
    const int index = NotificationSourceIndex(source);
    if (index < 0 || index >= kNotificationSourceCount)
        return;
    s_subDeadline[index] = 0;
    SetSubIslandTarget(index, false);
    if (s_hoveredSub == index)
        SetHoveredSub(-1);
    InvalidateIslandContent();
    RenderIslandFrame(true);
    RefreshSubDismissTimer();
}

bool IsSubIslandActive()
{
    return IsAnySubIslandVisible();
}

void ActivateSourceApp(NotificationSource source)
{
    if (source == NotificationSource::Other)
        return;

    WindowSearch search = {};
    search.source = source;
    EnumWindows(FindMessagingWindowProc, reinterpret_cast<LPARAM>(&search));

    HWND target = search.visibleResult;
    if (target == nullptr)
        target = search.hiddenResult;
    if (target == nullptr)
        return;

    if (IsIconic(target))
        ShowWindow(target, SW_RESTORE);
    else if (!IsWindowVisible(target))
        ShowWindow(target, SW_SHOW);

    if (SetForegroundWindow(target) == FALSE)
    {
        typedef void(WINAPI * SwitchToThisWindowFn)(HWND, BOOL);
        auto switchTo = reinterpret_cast<SwitchToThisWindowFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SwitchToThisWindow"));
        if (switchTo != nullptr)
            switchTo(target, TRUE);
    }
    BringWindowToTop(target);
}

void RequestNotificationAccessFromUi()
{
    RequestNotificationAccess();
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
    s_lastNotifyEnable = g_settings.notifyEnable;

    SetTimer(g_hwnd, kTimerContent, kContentCheckIntervalMs, nullptr);
    UpdateAutoHideTimerState();
    SetTimer(g_hwnd, kTimerTray, kTooltipRefreshIntervalMs, nullptr);

    RenderIslandFrame(true);
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    InitTrayIcon(g_hwnd);

    SetNotificationCallback(&OnAppNotification);
    InitNotifications(g_hwnd);
    InitChatWatcher(g_hwnd);

    if (wcsstr(GetCommandLineW(), L"--settings") != nullptr)
        PostMessageW(g_hwnd, kMsgShowSettings, 0, 0);

    // Internal visual-test hook: pop a sample sub-island without waiting for a
    // real WeChat/QQ message (used by the Lead's UI tests).
    if (wcsstr(GetCommandLineW(), L"--test-notify") != nullptr)
    {
        AppNotification sample;
        sample.id = 1;
        sample.source = NotificationSource::WeChat;
        sample.appId = L"Weixin.exe";
        sample.appName = L"微信";
        sample.title = L"张三";
        sample.body = L"晚上八点开会，记得带上电脑和资料";
        sample.arrivalTick = GetTickCount64();
        if (wcsstr(GetCommandLineW(), L"--test-notify-qq") != nullptr)
        {
            sample.source = NotificationSource::QQ;
            sample.appId = L"QQ";
            sample.appName = L"QQ";
            sample.title = L"产品群";
            sample.body = L"李四：新版设计稿已上传，麻烦看下";
        }
        ShowNotificationSubIsland(sample);
        if (wcsstr(GetCommandLineW(), L"--test-notify-both") != nullptr)
        {
            AppNotification second;
            second.id = 2;
            second.source = NotificationSource::QQ;
            second.appId = L"QQ";
            second.appName = L"QQ";
            second.title = L"产品群";
            second.body = L"李四：新版设计稿已上传，麻烦看下";
            second.arrivalTick = GetTickCount64();
            ShowNotificationSubIsland(second);
        }
    }

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
