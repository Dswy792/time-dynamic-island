// ===========================================================================
//  src/notifications.cpp
//
//  Windows toast listener for WeChat / QQ notifications (v5.1).
//
//  The module owns a worker thread that polls
//  Windows.UI.Notifications.Management.UserNotificationListener every ~800 ms.
//  Newly arrived toasts from WeChat or QQ are converted into heap
//  AppNotification payloads and posted to the island window as
//  kMsgNotification (WM_APP + 7, LPARAM = AppNotification*).  The window
//  procedure forwards the message to HandleNotificationMessage(), which
//  invokes the registered callback on the UI thread and releases the payload.
//
//  Only the documented Windows notification API is used; the WeChat / QQ
//  processes are never inspected or hooked.  Toasts that are still in the
//  notification centre when the module starts are folded into a baseline and
//  are never replayed to the UI.
// ===========================================================================

#include "notifications.h"

#include "app_state.h" // kMsgNotification (WM_APP + 7)

#include <winrt/base.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.UI.Notifications.Management.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>

namespace
{
// ---------------------------------------------------------------------------
// C++/WinRT types used by the worker
// ---------------------------------------------------------------------------
using winrt::Windows::UI::Notifications::KnownNotificationBindings;
using winrt::Windows::UI::Notifications::NotificationKinds;
using winrt::Windows::UI::Notifications::UserNotification;
using winrt::Windows::UI::Notifications::Management::UserNotificationListener;
using winrt::Windows::UI::Notifications::Management::UserNotificationListenerAccessStatus;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
constexpr DWORD kPollIntervalMs = 800;  // toast polling cadence
constexpr DWORD kFailureRetryMs = 2000; // back-off after a WinRT failure
constexpr DWORD kWaitResultStop = WAIT_OBJECT_0;
constexpr DWORD kWaitResultAccess = WAIT_OBJECT_0 + 1;

// ---------------------------------------------------------------------------
// Module state.  Init/Shutdown/RequestAccess are thread-safe; the worker
// thread only touches the atomics plus the worker-owned event handles.
// ---------------------------------------------------------------------------
std::mutex g_lifecycleMutex; // serialises InitNotifications / ShutdownNotifications
std::thread g_worker;
HANDLE g_stopEvent = nullptr;   // manual-reset: worker should terminate
HANDLE g_accessEvent = nullptr; // auto-reset: worker should run RequestAccessAsync()

std::atomic<bool> g_running{false};
std::atomic<HWND> g_targetHwnd{nullptr};
std::atomic<NotificationAccess> g_access{NotificationAccess::Unknown};
std::atomic<bool> g_accessRequested{false}; // request made before/while worker runs
std::atomic<uint32_t> g_maxNotificationId{0};

std::mutex g_callbackMutex;
void (*g_callback)(const AppNotification&) = nullptr;

std::once_flag g_exitHookOnce;

// ---------------------------------------------------------------------------
// Lifecycle helpers
// ---------------------------------------------------------------------------

// Last-resort join before CRT shutdown: if the host forgets to call
// ShutdownNotifications(), the static std::thread must not be destroyed while
// still joinable (std::terminate would be called by the CRT).
void EnsureExitHook()
{
    std::call_once(g_exitHookOnce, []() { std::atexit(&ShutdownNotifications); });
}

// ---------------------------------------------------------------------------
// Access status helpers
// ---------------------------------------------------------------------------
NotificationAccess ToNotificationAccess(UserNotificationListenerAccessStatus status)
{
    switch (status)
    {
    case UserNotificationListenerAccessStatus::Allowed:
        return NotificationAccess::Allowed;
    case UserNotificationListenerAccessStatus::Denied:
        return NotificationAccess::Denied;
    case UserNotificationListenerAccessStatus::Unspecified:
        return NotificationAccess::Unspecified;
    default:
        return NotificationAccess::Unknown;
    }
}

void RefreshAccessStatus(const UserNotificationListener& listener)
{
    try
    {
        g_access.store(ToNotificationAccess(listener.GetAccessStatus()), std::memory_order_relaxed);
    }
    catch (winrt::hresult_error const&)
    {
        // Keep the previous value; the worker retries.
    }
}

void PerformAccessRequest(const UserNotificationListener& listener)
{
    try
    {
        // Shows the system consent dialog when Windows has not decided yet.
        listener.RequestAccessAsync().get();
    }
    catch (winrt::hresult_error const&)
    {
        // Unsupported / denied: fall through and publish the real status.
    }
    RefreshAccessStatus(listener);
}

// ---------------------------------------------------------------------------
// Classification
// ---------------------------------------------------------------------------
std::wstring ToLowerAscii(std::wstring value)
{
    for (wchar_t& ch : value)
    {
        if (ch >= L'A' && ch <= L'Z')
        {
            ch = static_cast<wchar_t>(ch - L'A' + L'a');
        }
    }
    return value;
}

std::wstring NormalizeSeparators(std::wstring value)
{
    for (wchar_t& ch : value)
    {
        if (ch == L'/')
        {
            ch = L'\\';
        }
    }
    return value;
}

bool Contains(const std::wstring& haystack, const wchar_t* needle)
{
    return haystack.find(needle) != std::wstring::npos;
}

bool EndsWith(const std::wstring& value, const wchar_t* suffix)
{
    const std::wstring suffixText(suffix);
    if (value.size() < suffixText.size())
    {
        return false;
    }
    return value.compare(value.size() - suffixText.size(), suffixText.size(), suffixText) == 0;
}

// Other Tencent products also install below "...\Tencent\QQ...": QQ Music,
// QQ Browser, Tencent Video and QQLive live in folders whose names merely
// start with "QQ"/"Tencent".  They must never be reported as QQ messages.
bool IsOtherTencentProduct(const std::wstring& lowerId, const std::wstring& lowerName)
{
    static const wchar_t* const kProductTokens[] = {L"qqmusic", L"qqbrowser", L"tencentvideo", L"qqlive"};
    for (const wchar_t* token : kProductTokens)
    {
        if (Contains(lowerId, token) || Contains(lowerName, token))
        {
            return true;
        }
    }
    return false;
}

// Classifies a toast source from its AppUserModelId and display name.
// Case-insensitive; the AppUserModelId of unpackaged apps is often the full
// install path (e.g. "...\Weixin.exe"), so both separators are accepted.
NotificationSource ClassifySource(const std::wstring& appId, const std::wstring& appName)
{
    const std::wstring lowerId = ToLowerAscii(NormalizeSeparators(appId));
    const std::wstring lowerName = ToLowerAscii(NormalizeSeparators(appName));

    // WeChat: "Weixin.exe" install path (Weixin 4.x included), "Tencent.WeChat"
    // package ids, "WeixinApp" style ids (the "weixin" substring covers it) or
    // the Chinese display name.
    if (Contains(lowerId, L"weixin") || Contains(lowerId, L"wechat") ||
        Contains(lowerName, L"weixin") || Contains(lowerName, L"wechat") ||
        Contains(lowerId, L"微信") || Contains(lowerName, L"微信"))
    {
        return NotificationSource::WeChat;
    }

    // QQ Music / QQ Browser / Tencent Video / QQLive are separate products.
    if (IsOtherTencentProduct(lowerId, lowerName))
    {
        return NotificationSource::Other;
    }

    // QQ: exact AUMID "QQ" or exact display name "QQ", the QQ NT install path
    // (either separator, e.g. "...\Tencent\QQNT\QQ.exe") or the classic install
    // path ("...\Tencent\QQ\Bin\QQ.exe").  A prefix match alone is deliberately
    // NOT enough, otherwise QQ Music / QQ Browser / QQPinyin would match.
    if (lowerId == L"qq" || lowerName == L"qq" ||
        EndsWith(lowerId, L"\\qqnt\\qq.exe") ||
        EndsWith(lowerId, L"\\tencent\\qq\\bin\\qq.exe"))
    {
        return NotificationSource::QQ;
    }

    return NotificationSource::Other;
}

// ---------------------------------------------------------------------------
// Toast payload creation
// ---------------------------------------------------------------------------
void ExtractToastText(const UserNotification& notification, std::wstring& title, std::wstring& body)
{
    title.clear();
    body.clear();
    try
    {
        const auto toast = notification.Notification();
        if (!toast)
        {
            return;
        }
        const auto visual = toast.Visual();
        if (!visual)
        {
            return;
        }
        const auto binding = visual.GetBinding(KnownNotificationBindings::ToastGeneric());
        if (!binding)
        {
            return;
        }
        const auto texts = binding.GetTextElements();
        const uint32_t textCount = texts.Size();
        for (uint32_t i = 0; i < textCount; ++i)
        {
            const winrt::hstring text = texts.GetAt(i).Text();
            if (i == 0)
            {
                title.assign(text.c_str(), text.size());
            }
            else
            {
                if (!body.empty())
                {
                    body.push_back(L'\n');
                }
                body.append(text.c_str(), text.size());
            }
        }
    }
    catch (winrt::hresult_error const&)
    {
        // A malformed toast must not kill the listener; keep what we have.
    }
}

void EmitAppNotification(uint32_t id, NotificationSource source, std::wstring appId, std::wstring appName,
                         std::wstring title, std::wstring body)
{
    const HWND hwnd = g_targetHwnd.load(std::memory_order_acquire);
    if (!hwnd)
    {
        return;
    }

    AppNotification* payload = new (std::nothrow) AppNotification();
    if (!payload)
    {
        return;
    }
    payload->id = id;
    payload->source = source;
    payload->appId = std::move(appId);
    payload->appName = std::move(appName);
    payload->title = std::move(title);
    payload->body = std::move(body);
    payload->arrivalTick = GetTickCount64();

    if (!PostMessageW(hwnd, kMsgNotification, 0, reinterpret_cast<LPARAM>(payload)))
    {
        delete payload; // the window is already gone
    }
}

void EmitNotification(const UserNotification& notification, uint32_t id)
{
    try
    {
        const auto appInfo = notification.AppInfo();
        if (!appInfo)
        {
            return;
        }
        std::wstring appId = appInfo.AppUserModelId().c_str();
        std::wstring appName;
        const auto displayInfo = appInfo.DisplayInfo();
        if (displayInfo)
        {
            appName = displayInfo.DisplayName().c_str();
        }

        const NotificationSource source = ClassifySource(appId, appName);
        if (source == NotificationSource::Other)
        {
            return;
        }

        std::wstring title;
        std::wstring body;
        ExtractToastText(notification, title, body);
        EmitAppNotification(id, source, std::move(appId), std::move(appName), std::move(title), std::move(body));
    }
    catch (winrt::hresult_error const&)
    {
        // Skip this toast; polling continues.
    }
}

// ---------------------------------------------------------------------------
// Polling
// ---------------------------------------------------------------------------

// Remembers all toasts that already exist so that they are never replayed.
void SeedNotifications(const UserNotificationListener& listener)
{
    const auto notifications = listener.GetNotificationsAsync(NotificationKinds::Toast).get();
    const uint32_t count = notifications.Size();
    uint32_t maxId = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint32_t id = notifications.GetAt(i).Id();
        if (id > maxId)
        {
            maxId = id;
        }
    }
    g_maxNotificationId.store(maxId, std::memory_order_relaxed);
}

void PollNotifications(const UserNotificationListener& listener)
{
    const auto notifications = listener.GetNotificationsAsync(NotificationKinds::Toast).get();
    const uint32_t count = notifications.Size();
    const uint32_t previousMax = g_maxNotificationId.load(std::memory_order_relaxed);
    uint32_t newMax = previousMax;

    for (uint32_t i = 0; i < count; ++i)
    {
        try
        {
            const auto notification = notifications.GetAt(i);
            const uint32_t id = notification.Id();
            if (id > newMax)
            {
                newMax = id;
            }
            if (id <= previousMax)
            {
                continue; // historical toast, already seeded
            }
            EmitNotification(notification, id);
        }
        catch (winrt::hresult_error const&)
        {
            // Skip this entry, but keep polling.
        }
    }

    g_maxNotificationId.store(newMax, std::memory_order_relaxed);
}

// Waits for the next poll tick or for a stop/access-request event.
DWORD WaitForWorkerEvents(DWORD timeoutMs)
{
    const HANDLE stopEvent = g_stopEvent;
    const HANDLE accessEvent = g_accessEvent;
    if (!stopEvent || !accessEvent)
    {
        Sleep(timeoutMs);
        return WAIT_TIMEOUT;
    }
    const HANDLE events[2] = {stopEvent, accessEvent};
    return WaitForMultipleObjects(2, events, FALSE, timeoutMs);
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------
void WorkerMain()
{
    bool apartmentInitialized = false;
    try
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        apartmentInitialized = true;
    }
    catch (...)
    {
        // The retry loop below keeps running; a missing WinRT apartment simply
        // makes every WinRT call fail temporarily.
    }

    try
    {
        UserNotificationListener listener{nullptr};
        bool seeded = false;

        while (g_running.load(std::memory_order_acquire))
        {
            bool failed = false;
            try
            {
                if (!listener)
                {
                    listener = UserNotificationListener::Current();
                    RefreshAccessStatus(listener);
                    // Automatic first-run consent plus a request that was made
                    // before the worker started.
                    if (g_accessRequested.exchange(false) ||
                        g_access.load(std::memory_order_relaxed) == NotificationAccess::Unspecified)
                    {
                        PerformAccessRequest(listener);
                    }
                }

                if (!seeded)
                {
                    SeedNotifications(listener);
                    seeded = true;
                }

                const DWORD waitResult = WaitForWorkerEvents(kPollIntervalMs);
                if (!g_running.load(std::memory_order_acquire) || waitResult == kWaitResultStop)
                {
                    break;
                }
                if (waitResult == kWaitResultAccess)
                {
                    PerformAccessRequest(listener);
                    continue;
                }
                PollNotifications(listener);
            }
            catch (winrt::hresult_error const&)
            {
                failed = true;
            }
            catch (std::exception const&)
            {
                failed = true;
            }

            if (failed)
            {
                // Never let a WinRT hiccup terminate the listener: back off and
                // retry, while still reacting to stop / access requests.
                WaitForWorkerEvents(kFailureRetryMs);
                if (!g_running.load(std::memory_order_acquire))
                {
                    break;
                }
            }
        }
    }
    catch (...)
    {
        // Nothing may escape the worker thread.
    }

    if (apartmentInitialized)
    {
        try
        {
            winrt::uninit_apartment();
        }
        catch (...)
        {
        }
    }
}
} // namespace

// ===========================================================================
// Public API (include/notifications.h)
// ===========================================================================

void InitNotifications(HWND hwnd)
{
    EnsureExitHook();

    std::lock_guard<std::mutex> lock(g_lifecycleMutex);
    g_targetHwnd.store(hwnd, std::memory_order_release);

    if (g_running.load(std::memory_order_acquire))
    {
        return; // idempotent: the worker is already polling
    }

    if (g_worker.joinable())
    {
        g_worker.join(); // defensive: a previous shutdown left it running
    }

    if (!g_stopEvent)
    {
        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); // manual reset
    }
    if (!g_accessEvent)
    {
        g_accessEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr); // auto reset
    }
    if (!g_stopEvent || !g_accessEvent)
    {
        if (g_stopEvent)
        {
            CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
        }
        if (g_accessEvent)
        {
            CloseHandle(g_accessEvent);
            g_accessEvent = nullptr;
        }
        return;
    }

    ResetEvent(g_stopEvent);
    ResetEvent(g_accessEvent);
    g_running.store(true, std::memory_order_release);
    try
    {
        g_worker = std::thread(&WorkerMain);
    }
    catch (...)
    {
        g_running.store(false, std::memory_order_release);
    }
}

void ShutdownNotifications()
{
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(g_lifecycleMutex);
        g_running.store(false, std::memory_order_release);
        if (g_stopEvent)
        {
            SetEvent(g_stopEvent);
        }
        if (g_worker.joinable())
        {
            worker = std::move(g_worker);
        }
    }

    if (worker.joinable())
    {
        worker.join();
    }

    {
        std::lock_guard<std::mutex> lock(g_lifecycleMutex);
        if (g_stopEvent)
        {
            CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
        }
        if (g_accessEvent)
        {
            CloseHandle(g_accessEvent);
            g_accessEvent = nullptr;
        }
        g_targetHwnd.store(nullptr, std::memory_order_release);
    }
}

void HandleNotificationMessage(WPARAM, LPARAM lParam)
{
    AppNotification* notification = reinterpret_cast<AppNotification*>(lParam);
    if (!notification)
    {
        return;
    }

    void (*callback)(const AppNotification&) = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        callback = g_callback;
    }
    if (callback)
    {
        callback(*notification);
    }
    delete notification;
}

NotificationAccess GetNotificationAccessStatus()
{
    return g_access.load(std::memory_order_acquire);
}

void RequestNotificationAccess()
{
    std::lock_guard<std::mutex> lock(g_lifecycleMutex);
    g_accessRequested.store(true, std::memory_order_release);
    if (g_running.load(std::memory_order_acquire) && g_accessEvent)
    {
        SetEvent(g_accessEvent);
    }
    // If the worker is not running yet the flag is honoured by InitNotifications()
    // / the first worker iteration.
}

void SetNotificationCallback(void (*callback)(const AppNotification& notification))
{
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    g_callback = callback;
}
