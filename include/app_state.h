#pragma once

#include <windows.h>

// ---------------------------------------------------------------------------
// Island configuration enums (persisted in the registry as DWORDs)
// ---------------------------------------------------------------------------
enum IslandSize
{
    ISLAND_SIZE_SMALL = 0,
    ISLAND_SIZE_MEDIUM = 1,
    ISLAND_SIZE_LARGE = 2,
    ISLAND_SIZE_COUNT = 3
};

enum IslandPosition
{
    ISLAND_POS_LEFT = 0,
    ISLAND_POS_CENTER = 1,
    ISLAND_POS_RIGHT = 2,
    ISLAND_POS_CUSTOM = 3
};

enum IslandTheme
{
    ISLAND_THEME_DARK = 0,
    ISLAND_THEME_LIGHT = 1,
    ISLAND_THEME_SYSTEM = 2
};

enum IslandDateFormat
{
    ISLAND_DATE_MMDD = 0, // 10/28 or 2026/10/28
    ISLAND_DATE_DDMM = 1, // 28/10 or 28/10/2026
    ISLAND_DATE_YMD = 2,  // 2026-10-28
    ISLAND_DATE_CN = 3,   // 10月28日 or 2026年10月28日
    ISLAND_DATE_COUNT = 4
};

// ---------------------------------------------------------------------------
// Shared identifiers
// ---------------------------------------------------------------------------
constexpr wchar_t kIslandWindowClass[] = L"DynamicIslandClass";
constexpr wchar_t kIslandMutexName[] = L"DynamicIsland_Mutex";
constexpr wchar_t kSettingsRegistryKey[] = L"SOFTWARE\\DynamicIsland";
constexpr wchar_t kRunRegistryKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";

constexpr UINT kTimerContent = 1;     // periodic clock change detection
constexpr UINT kTimerAutoHide = 2;    // mouse / fullscreen auto-hide monitor
constexpr UINT kTimerAnimation = 3;   // animation tick (~60 fps)
constexpr UINT kTimerTray = 4;        // tray tooltip refresh
constexpr UINT kTimerNotificationDismiss = 5; // hide the message peek again

constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kMsgShowSettings = WM_APP + 2;     // second instance / tray icon
constexpr UINT kMsgApplySettings = WM_APP + 3;    // settings dialog finished applying
constexpr UINT kMsgRefreshTheme = WM_APP + 4;     // system theme changed
constexpr UINT kMsgRequestExit = WM_APP + 5;      // exit requested by UI component
constexpr UINT kMsgIslandClicked = WM_APP + 6;    // reserved for future use
constexpr UINT kMsgNotification = WM_APP + 7;     // notifications module -> UI thread

// Numeric ranges (shared with the settings UI)
constexpr int kFontSizeMin = 9;
constexpr int kFontSizeMax = 32;
constexpr int kOpacityMin = 55;
constexpr int kOpacityMax = 100;
constexpr int kHideDelayMinSeconds = 0;
constexpr int kHideDelayMaxSeconds = 3600;

// Message notification options. The sub-island stays on the island until it is
// clicked (or the source app is brought to the foreground); 0 disables the
// optional auto-collapse, 1..10 = automatically collapse after that many
// seconds.
constexpr int kNotifyPeekSecondsMin = 0;
constexpr int kNotifyPeekSecondsMax = 10;
constexpr int kNotifyPeekSecondsDefault = 0;

constexpr int kTooltipRefreshIntervalMs = 20000;
constexpr int kAutoHideCheckIntervalMs = 100;
constexpr int kContentCheckIntervalMs = 200;
constexpr int kAnimationTickMs = 16;

// ---------------------------------------------------------------------------
// Persisted settings
// ---------------------------------------------------------------------------
struct AppSettings
{
    bool use24Hour = true;
    bool showSeconds = true;
    bool showAmPm = true;
    bool showDate = true;
    bool showYear = false;
    bool showWeekday = false;
    int dateFormat = ISLAND_DATE_MMDD;
    int theme = ISLAND_THEME_DARK;
    int windowSize = ISLAND_SIZE_MEDIUM;
    int position = ISLAND_POS_CENTER;
    int customX = -100000; // capsule top-left in screen pixels, only for ISLAND_POS_CUSTOM
    int customY = -100000;
    bool autoHide = false;
    int hideDelaySeconds = 3;
    bool hideOnFullscreen = false;
    int fontSize = 14;
    bool boldFont = true;
    bool autoStart = false;
    int opacity = 100; // 55..100 percent -> capsule alpha

    // New message notifications from WeChat / QQ (Windows toast listener).
    bool notifyEnable = true;
    bool notifyShowContent = true;
    bool notifyWeChat = true;
    bool notifyQQ = true;
    int notifyPeekSeconds = kNotifyPeekSecondsDefault; // 0 = stay, 1..10 = auto-collapse
};

extern AppSettings g_settings;
extern HWND g_hwnd;
