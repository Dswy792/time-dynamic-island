#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

// Notifications are read through the Windows notification listener
// (Windows.UI.Notifications.Management.UserNotificationListener). The app never
// touches the WeChat/QQ processes and never invokes hooks.
enum class NotificationSource
{
    Other = 0,
    WeChat = 1,
    QQ = 2
};

// Compact index used by the sub-island animation / renderer arrays.
constexpr int kNotificationSourceCount = 2; // 0 = WeChat, 1 = QQ

inline int NotificationSourceIndex(NotificationSource source)
{
    return source == NotificationSource::QQ ? 1 : 0;
}

struct AppNotification
{
    uint32_t id = 0;
    NotificationSource source = NotificationSource::Other;
    std::wstring appId;   // AppUserModelId (e.g. "QQ")
    std::wstring appName; // display name (e.g. "微信", "QQ")
    std::wstring title;   // first toast text line (sender / subject)
    std::wstring body;    // remaining toast text lines joined
    ULONGLONG arrivalTick = 0;
};

enum class NotificationAccess
{
    Unknown = 0,
    Unspecified = 1,
    Denied = 2,
    Allowed = 3
};

// Start the listener. A worker thread polls the WinRT API once per second,
// classifies WeChat/QQ toasts and posts kMsgNotification (heap AppNotification*)
// to the given window. The worker requests notification access automatically
// when the state is still "Unspecified".
void InitNotifications(HWND hwnd);
void ShutdownNotifications();

// Forward WM_APP notification messages from the island window procedure here.
// Calls the registered callback on the UI thread and releases the payload.
void HandleNotificationMessage(WPARAM wParam, LPARAM lParam);

// Current access state (pollable from the settings UI).
NotificationAccess GetNotificationAccessStatus();

// Ask Windows for notification access (shows the system consent dialog once).
// Thread-safe; the request is executed on the notification worker thread.
void RequestNotificationAccess();

// Invoked on the UI thread for every new WeChat/QQ notification.
void SetNotificationCallback(void (*callback)(const AppNotification& notification));
