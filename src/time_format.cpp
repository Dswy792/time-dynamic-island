#include "time_format.h"

#include <windows.h>

#include <string>

#include "version.h"

namespace
{
// Appends a zero-padded decimal number without relying on the CRT locale or
// printf format strings.
void AppendPadded(std::wstring& text, unsigned int value, int width)
{
    wchar_t digits[16];
    int count = 0;
    do
    {
        digits[count++] = static_cast<wchar_t>(L'0' + value % 10u);
        value /= 10u;
    } while (value != 0);

    for (int i = count; i < width; ++i)
        text.push_back(L'0');
    while (count > 0)
        text.push_back(digits[--count]);
}

std::wstring FormatTimeTextAt(const AppSettings& settings, const SYSTEMTIME& now)
{
    unsigned int hour = now.wHour;
    bool isPm = false;
    if (!settings.use24Hour)
    {
        isPm = hour >= 12;
        hour %= 12;
        if (hour == 0)
            hour = 12;
    }

    std::wstring text;
    AppendPadded(text, hour, 2);
    text.push_back(L':');
    AppendPadded(text, now.wMinute, 2);
    if (settings.showSeconds)
    {
        text.push_back(L':');
        AppendPadded(text, now.wSecond, 2);
    }

    if (!settings.use24Hour && settings.showAmPm)
        text += isPm ? L" PM" : L" AM";

    return text;
}

// Localized short weekday name ("ddd"), e.g. "周二" / "Tue". Returns an empty
// string when Windows cannot format the name for the current locale.
std::wstring FormatWeekdayText(const SYSTEMTIME& now)
{
    wchar_t buffer[64] = {};
    const int length = GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &now, L"ddd", buffer,
                                       static_cast<int>(ARRAYSIZE(buffer)), nullptr);
    if (length <= 1)
        return std::wstring();
    return std::wstring(buffer, static_cast<size_t>(length - 1));
}

std::wstring FormatDateTextAt(const AppSettings& settings, const SYSTEMTIME& now)
{
    const unsigned int year = now.wYear;
    const unsigned int month = now.wMonth;
    const unsigned int day = now.wDay;

    std::wstring text;
    switch (settings.dateFormat)
    {
    case ISLAND_DATE_DDMM:
        AppendPadded(text, day, 2);
        text.push_back(L'/');
        AppendPadded(text, month, 2);
        if (settings.showYear)
        {
            text.push_back(L'/');
            AppendPadded(text, year, 4);
        }
        break;

    case ISLAND_DATE_YMD:
        AppendPadded(text, year, 4);
        text.push_back(L'-');
        AppendPadded(text, month, 2);
        text.push_back(L'-');
        AppendPadded(text, day, 2);
        break;

    case ISLAND_DATE_CN:
        if (settings.showYear)
        {
            AppendPadded(text, year, 1);
            text += L"\u5e74";
        }
        AppendPadded(text, month, 1);
        text += L"\u6708";
        AppendPadded(text, day, 1);
        text += L"\u65e5";
        break;

    case ISLAND_DATE_MMDD:
    default:
        if (settings.showYear)
        {
            AppendPadded(text, year, 4);
            text.push_back(L'/');
        }
        AppendPadded(text, month, 2);
        text.push_back(L'/');
        AppendPadded(text, day, 2);
        break;
    }

    if (settings.showWeekday)
    {
        const std::wstring weekday = FormatWeekdayText(now);
        if (!weekday.empty())
        {
            text.push_back(L' ');
            text += weekday;
        }
    }

    return text;
}

SYSTEMTIME GetLocalTimeNow()
{
    SYSTEMTIME now = {};
    GetLocalTime(&now);
    return now;
}
} // namespace

std::wstring FormatTimeText(const AppSettings& settings)
{
    return FormatTimeTextAt(settings, GetLocalTimeNow());
}

std::wstring FormatDateText(const AppSettings& settings)
{
    if (!settings.showDate)
        return std::wstring();
    return FormatDateTextAt(settings, GetLocalTimeNow());
}

std::wstring FormatDisplayText(const AppSettings& settings)
{
    const SYSTEMTIME now = GetLocalTimeNow();
    const std::wstring time = FormatTimeTextAt(settings, now);
    if (!settings.showDate)
        return time;

    const std::wstring date = FormatDateTextAt(settings, now);
    if (date.empty())
        return time;
    return date + L"  " + time;
}

std::wstring FormatTooltipText(const AppSettings& settings)
{
    return std::wstring(APP_NAME) + L" \u2014 " + FormatDisplayText(settings);
}

std::wstring GetTimeText()
{
    return FormatTimeText(g_settings);
}

std::wstring GetDateText()
{
    return FormatDateText(g_settings);
}

std::wstring GetDisplayText()
{
    return FormatDisplayText(g_settings);
}

std::wstring GetTooltipText()
{
    return FormatTooltipText(g_settings);
}
