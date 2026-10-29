// ===========================================================================
//  src/chat_watcher.cpp
//
//  WeChat 4.x (Weixin.exe) does not register itself with the Windows
//  notification centre, so the toast listener can never see its messages.
//  This module reads the WeChat conversation list through the documented
//  Windows UI Automation (UIA) API and posts new incoming messages to the
//  island exactly like the toast listener does.
//
//  It does NOT inject into WeChat, hook any API, read process memory or touch
//  the WeChat database. It only reads accessible UI text from the window, the
//  same data screen readers use.
//
//  Detection:
//   * poll the conversation list ("会话" list, automation id "session_list")
//   * each item's accessible name contains: chat title, optional 已置顶 /
//     消息免打扰 markers, the last message preview and its time
//   * when the preview/time of a non-active conversation changes to a message
//     from today (or the item jumps to the top), a notification is emitted
//   * the active conversation is skipped (that is the chat the user is
//     already looking at, and it also filters out the user's own messages)
// ===========================================================================

#include "chat_watcher.h"

#include <windows.h>
#include <objbase.h>
#include <uiautomation.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app_state.h"
#include "notifications.h"

namespace
{
// ---------------------------------------------------------------------------
// Debug logging (opt-in via the DSH_ISLAND_DEBUG environment variable)
// ---------------------------------------------------------------------------
void ChatLog(const char* format, ...)
{
    if (GetEnvironmentVariableW(L"DSH_ISLAND_DEBUG", nullptr, 0) == 0)
        return;
    FILE* file = nullptr;
    if (fopen_s(&file, "chat-watcher.log", "a") != 0 || file == nullptr)
        return;
    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fclose(file);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
std::mutex g_lifecycleMutex;
std::thread g_worker;
std::atomic<bool> g_running{false};
std::atomic<HWND> g_targetHwnd{nullptr};
std::once_flag g_exitHookOnce;

struct SessionItem
{
    std::wstring id;
    std::wstring title;
    std::wstring preview;
    std::wstring time;
    bool selected = false;
};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
std::wstring GetStringProperty(IUIAutomationElement* element, PROPERTYID property)
{
    std::wstring result;
    if (element == nullptr)
        return result;
    VARIANT value;
    VariantInit(&value);
    if (SUCCEEDED(element->GetCurrentPropertyValue(property, &value)))
    {
        if (value.vt == VT_BSTR && value.bstrVal != nullptr)
            result = value.bstrVal;
        else if (value.vt == VT_I4)
            result = std::to_wstring(value.lVal);
    }
    VariantClear(&value);
    return result;
}

bool GetBoolProperty(IUIAutomationElement* element, PROPERTYID property,
                     bool defaultValue)
{
    if (element == nullptr)
        return defaultValue;
    VARIANT value;
    VariantInit(&value);
    bool result = defaultValue;
    if (SUCCEEDED(element->GetCurrentPropertyValue(property, &value)) &&
        value.vt == VT_BOOL)
    {
        result = value.boolVal != VARIANT_FALSE;
    }
    VariantClear(&value);
    return result;
}

std::wstring Trim(std::wstring value)
{
    const wchar_t* whitespace = L" \t\r\n";
    const size_t begin = value.find_first_not_of(whitespace);
    if (begin == std::wstring::npos)
        return std::wstring();
    const size_t end = value.find_last_not_of(whitespace);
    return value.substr(begin, end - begin + 1);
}

std::vector<std::wstring> SplitLines(const std::wstring& text)
{
    std::vector<std::wstring> lines;
    std::wstring current;
    for (wchar_t ch : text)
    {
        if (ch == L'\n' || ch == L'\r')
        {
            const std::wstring line = Trim(current);
            if (!line.empty())
                lines.push_back(line);
            current.clear();
        }
        else
        {
            current.push_back(ch);
        }
    }
    const std::wstring line = Trim(current);
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

bool IsTimeLine(const std::wstring& line)
{
    if (line.empty())
        return false;
    // HH:mm
    if (line.size() >= 3 && line.size() <= 5 && line.find(L':') != std::wstring::npos)
    {
        bool digits = true;
        for (wchar_t ch : line)
        {
            if (ch != L':' && (ch < L'0' || ch > L'9'))
            {
                digits = false;
                break;
            }
        }
        if (digits)
            return true;
    }
    // 昨天 / 星期X / MM/DD / YYYY/MM/DD
    if (line.rfind(L"昨天", 0) == 0 || line.rfind(L"星期", 0) == 0)
        return true;
    int slashes = 0;
    for (wchar_t ch : line)
    {
        if (ch == L'/')
            ++slashes;
    }
    return slashes >= 1 && slashes <= 2;
}

bool IsTodayTime(const std::wstring& time)
{
    if (time.empty() || time.find(L':') == std::wstring::npos)
        return false;
    for (wchar_t ch : time)
    {
        if (ch != L':' && (ch < L'0' || ch > L'9'))
            return false;
    }
    return true;
}

bool IsMarkerLine(const std::wstring& line)
{
    return line == L"已置顶" || line == L"消息免打扰" || line == L"置顶";
}

std::wstring StripAggregatePrefix(std::wstring text)
{
    // "[104条] 沐颜: xxx" -> "沐颜: xxx"
    if (!text.empty() && text.front() == L'[')
    {
        const size_t close = text.find(L']');
        if (close != std::wstring::npos && close > 1)
            text = Trim(text.substr(close + 1));
    }
    return text;
}

SessionItem ParseSessionItem(const std::wstring& name, const std::wstring& id,
                             bool selected)
{
    SessionItem item;
    item.id = id;
    item.selected = selected;

    std::vector<std::wstring> lines = SplitLines(name);
    if (lines.empty())
        return item;

    item.title = lines.front();
    size_t index = 1;
    while (index < lines.size() && IsMarkerLine(lines[index]))
        ++index;

    std::vector<std::wstring> rest;
    for (; index < lines.size(); ++index)
        rest.push_back(lines[index]);

    if (!rest.empty() && IsTimeLine(rest.back()))
    {
        item.time = rest.back();
        rest.pop_back();
    }

    std::wstring preview;
    for (const std::wstring& part : rest)
    {
        if (!preview.empty())
            preview += L" ";
        preview += part;
    }
    item.preview = StripAggregatePrefix(preview);
    return item;
}

// ---------------------------------------------------------------------------
// Window discovery
// ---------------------------------------------------------------------------
struct WindowSearch
{
    HWND result;
    bool found;
};

bool ProcessIsWeChat(DWORD processId)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
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
    return _wcsicmp(base, L"Weixin.exe") == 0 || _wcsicmp(base, L"WeChat.exe") == 0 ||
           _wcsicmp(base, L"WeixinApp.exe") == 0;
}

BOOL CALLBACK FindWeChatWindowProc(HWND hwnd, LPARAM param)
{
    WindowSearch* search = reinterpret_cast<WindowSearch*>(param);
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == 0 || !ProcessIsWeChat(processId))
        return TRUE;

    wchar_t className[256] = {};
    if (GetClassNameW(hwnd, className, 256) == 0)
        return TRUE;
    if (wcsstr(className, L"QWindowIcon") == nullptr)
        return TRUE;

    search->result = hwnd;
    search->found = true;
    return FALSE;
}

HWND FindWeChatMainWindow()
{
    WindowSearch search = {};
    EnumWindows(FindWeChatWindowProc, reinterpret_cast<LPARAM>(&search));
    return search.result;
}

// ---------------------------------------------------------------------------
// Snapshot + detection
// ---------------------------------------------------------------------------
bool EmitNotification(const std::wstring& title, const std::wstring& body)
{
    const HWND hwnd = g_targetHwnd.load(std::memory_order_acquire);
    if (hwnd == nullptr)
        return false;

    AppNotification* payload = new (std::nothrow) AppNotification();
    if (payload == nullptr)
        return false;
    static std::atomic<uint32_t> nextId{1};
    payload->id = nextId.fetch_add(1, std::memory_order_relaxed);
    payload->source = NotificationSource::WeChat;
    payload->appId = L"weixin-ui";
    payload->appName = L"微信";
    payload->title = title;
    payload->body = body.size() > 200 ? body.substr(0, 200) : body;
    payload->arrivalTick = GetTickCount64();

    if (!PostMessageW(hwnd, kMsgNotification, 0,
                      reinterpret_cast<LPARAM>(payload)))
    {
        delete payload;
        return false;
    }
    ChatLog("emit wechat title=%ls body=%ls\n", title.c_str(), payload->body.c_str());
    return true;
}

class SessionWatcher
{
public:
    void Reset()
    {
        m_hasSnapshot = false;
        m_items.clear();
    }

    void HandleSnapshot(const std::vector<SessionItem>& items)
    {
        if (!m_hasSnapshot)
        {
            m_items = items;
            m_hasSnapshot = true;
            ChatLog("seed %zu items\n", m_items.size());
            return;
        }

        for (size_t index = 0; index < items.size(); ++index)
        {
            const SessionItem& item = items[index];
            const SessionItem* previous = FindPrevious(item.id);
            if (previous == nullptr)
            {
                ChatLog("new row index=%zu title=%ls preview=%ls time=%ls sel=%d\n",
                        index, item.title.c_str(), item.preview.c_str(),
                        item.time.c_str(), item.selected ? 1 : 0);
                // A brand new conversation row usually means a fresh chat.
                if (index == 0 && !item.selected && IsTodayTime(item.time))
                    EmitNotification(item.title, item.preview);
                continue;
            }

            const bool changed = item.preview != previous->preview ||
                                 item.time != previous->time;
            if (changed)
            {
                ChatLog("changed index=%zu title=%ls prev=[%ls/%ls] now=[%ls/%ls] sel=%d\n",
                        index, item.title.c_str(), previous->preview.c_str(),
                        previous->time.c_str(), item.preview.c_str(),
                        item.time.c_str(), item.selected ? 1 : 0);
            }
            if (!changed || item.selected)
                continue;

            const bool recent = IsTodayTime(item.time) || index == 0;
            if (recent && !item.preview.empty())
                EmitNotification(item.title, item.preview);
        }

        m_items = items;
    }

private:
    const SessionItem* FindPrevious(const std::wstring& id) const
    {
        for (const SessionItem& item : m_items)
        {
            if (item.id == id)
                return &item;
        }
        return nullptr;
    }

    bool m_hasSnapshot = false;
    std::vector<SessionItem> m_items;
};

std::vector<SessionItem> ReadSessions(IUIAutomation* automation, HWND wechatHwnd,
                                      IUIAutomationElement** cachedRoot,
                                      IUIAutomationElement** cachedList)
{
    std::vector<SessionItem> result;
    if (automation == nullptr || wechatHwnd == nullptr)
        return result;

    if (*cachedRoot == nullptr)
    {
        if (FAILED(automation->ElementFromHandle(wechatHwnd, cachedRoot)))
        {
            *cachedRoot = nullptr;
            return result;
        }
    }
    if (*cachedList == nullptr)
    {
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_BSTR;
        value.bstrVal = SysAllocString(L"session_list");
        IUIAutomationCondition* condition = nullptr;
        automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, value,
                                            &condition);
        VariantClear(&value);
        if (condition == nullptr)
            return result;
        (*cachedRoot)->FindFirst(TreeScope_Descendants, condition, cachedList);
        condition->Release();
        if (*cachedList == nullptr)
            return result;
    }

    IUIAutomationTreeWalker* walker = nullptr;
    automation->get_RawViewWalker(&walker);
    IUIAutomationCondition* trueCondition = nullptr;
    automation->CreateTrueCondition(&trueCondition);

    IUIAutomationElementArray* children = nullptr;
    if (SUCCEEDED((*cachedList)->FindAll(TreeScope_Children, trueCondition, &children)) &&
        children != nullptr)
    {
        int count = 0;
        children->get_Length(&count);
        for (int i = 0; i < count; ++i)
        {
            IUIAutomationElement* child = nullptr;
            if (FAILED(children->GetElement(i, &child)) || child == nullptr)
                continue;

            const std::wstring name = GetStringProperty(child, UIA_NamePropertyId);
            const std::wstring id =
                GetStringProperty(child, UIA_AutomationIdPropertyId);
            const bool selected =
                GetBoolProperty(child, UIA_SelectionItemIsSelectedPropertyId, false);
            if (!name.empty())
                result.push_back(ParseSessionItem(name, id, selected));
            child->Release();
        }
        children->Release();
    }

    if (trueCondition != nullptr)
        trueCondition->Release();
    if (walker != nullptr)
        walker->Release();
    return result;
}

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------
void WorkerMain()
{
    HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IUIAutomation* automation = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr,
                                   CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))))
    {
        // Nothing else to do: the interface is ready.
    }
    else
    {
        automation = nullptr;
    }

    SessionWatcher watcher;
    IUIAutomationElement* cachedRoot = nullptr;
    IUIAutomationElement* cachedList = nullptr;
    HWND cachedWindow = nullptr;
    int emptyPolls = 0;

    while (g_running.load(std::memory_order_acquire))
    {
        const HWND target = g_targetHwnd.load(std::memory_order_acquire);
        if (target == nullptr)
        {
            Sleep(500);
            continue;
        }

        const HWND wechat = FindWeChatMainWindow();
        if (wechat == nullptr)
        {
            if (cachedRoot != nullptr)
            {
                cachedRoot->Release();
                cachedRoot = nullptr;
            }
            if (cachedList != nullptr)
            {
                cachedList->Release();
                cachedList = nullptr;
            }
            cachedWindow = nullptr;
            watcher.Reset();
            Sleep(1000);
            continue;
        }

        if (wechat != cachedWindow)
        {
            if (cachedRoot != nullptr)
            {
                cachedRoot->Release();
                cachedRoot = nullptr;
            }
            if (cachedList != nullptr)
            {
                cachedList->Release();
                cachedList = nullptr;
            }
            cachedWindow = wechat;
            watcher.Reset();
            ChatLog("wechat window %p\n", wechat);
        }

        if (automation != nullptr)
        {
            std::vector<SessionItem> items =
                ReadSessions(automation, wechat, &cachedRoot, &cachedList);
            ChatLog("poll items=%zu list=%d root=%d\n", items.size(),
                    cachedList != nullptr ? 1 : 0, cachedRoot != nullptr ? 1 : 0);
            if (!items.empty())
            {
                emptyPolls = 0;
                watcher.HandleSnapshot(items);
            }
            else if (cachedList != nullptr)
            {
                // The window was rebuilt; retry the element lookup next round.
                if (++emptyPolls >= 2)
                {
                    cachedList->Release();
                    cachedList = nullptr;
                    emptyPolls = 0;
                }
            }
        }

        for (int i = 0; i < 10 && g_running.load(std::memory_order_acquire); ++i)
            Sleep(100);
    }

    if (cachedRoot != nullptr)
        cachedRoot->Release();
    if (cachedList != nullptr)
        cachedList->Release();
    if (automation != nullptr)
        automation->Release();
    if (SUCCEEDED(comResult))
        CoUninitialize();
}

void EnsureExitHook()
{
    std::call_once(g_exitHookOnce, []() { std::atexit(&ShutdownChatWatcher); });
}
} // namespace

void InitChatWatcher(HWND targetHwnd)
{
    EnsureExitHook();
    std::lock_guard<std::mutex> lock(g_lifecycleMutex);
    g_targetHwnd.store(targetHwnd, std::memory_order_release);
    if (g_running.load(std::memory_order_acquire))
        return;
    if (g_worker.joinable())
        g_worker.join();
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

void ShutdownChatWatcher()
{
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(g_lifecycleMutex);
        g_running.store(false, std::memory_order_release);
        if (g_worker.joinable())
            worker = std::move(g_worker);
    }
    if (worker.joinable())
        worker.join();
    std::lock_guard<std::mutex> lock(g_lifecycleMutex);
    g_targetHwnd.store(nullptr, std::memory_order_release);
}
