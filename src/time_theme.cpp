#include "time_theme.h"

#include <chrono>
#include <iomanip>
#include <sstream>

#include "app_state.h"

bool IsLightTheme()
{
    DWORD value = 0;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD size = sizeof(value);
        RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL,
                         reinterpret_cast<LPBYTE>(&value), &size);
        RegCloseKey(hKey);
    }
    return value == 1;
}

std::wstring GetTimeString()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm tm;
    localtime_s(&tm, &time);

    std::wstringstream wss;
    int hour = g_use24Hour ? tm.tm_hour : (tm.tm_hour % 12);
    if (hour == 0 && !g_use24Hour)
        hour = 12;

    wss << std::setfill(L'0') << std::setw(2) << hour << L":"
        << std::setw(2) << tm.tm_min;

    if (g_showSeconds)
    {
        wss << L":" << std::setw(2) << tm.tm_sec;
    }

    return wss.str();
}

std::wstring GetDateString()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm tm;
    localtime_s(&tm, &time);

    std::wstringstream wss;

    if (g_showYear)
    {
        wss << (tm.tm_year + 1900) << L"/"
            << std::setfill(L'0') << std::setw(2) << (tm.tm_mon + 1) << L"/"
            << std::setw(2) << tm.tm_mday;
    }
    else
    {
        wss << std::setfill(L'0') << std::setw(2) << (tm.tm_mon + 1) << L"/"
            << std::setw(2) << tm.tm_mday;
    }

    return wss.str();
}

COLORREF GetBackgroundColor()
{
    if (g_theme == 0)
    {
        return RGB(30, 30, 30);
    }
    else if (g_theme == 1)
    {
        return RGB(255, 255, 255);
    }
    else
    {
        if (IsLightTheme())
        {
            return RGB(255, 255, 255);
        }
        else
        {
            return RGB(30, 30, 30);
        }
    }
}

COLORREF GetTextColor()
{
    if (g_theme == 0)
    {
        return RGB(255, 255, 255);
    }
    else if (g_theme == 1)
    {
        return RGB(0, 0, 0);
    }
    else
    {
        if (IsLightTheme())
        {
            return RGB(0, 0, 0);
        }
        else
        {
            return RGB(255, 255, 255);
        }
    }
}
