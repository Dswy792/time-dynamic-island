#include "settings.h"

#include <windows.h>

#include <string>

#include "app_state.h"

namespace
{
constexpr DWORD kSettingsVersion = 2;

bool ReadDword(HKEY key, const wchar_t* name, DWORD* value)
{
    DWORD type = 0;
    DWORD size = sizeof(DWORD);
    DWORD data = 0;
    if (RegQueryValueExW(key, name, nullptr, &type,
                         reinterpret_cast<LPBYTE>(&data), &size) != ERROR_SUCCESS)
        return false;
    if (type != REG_DWORD || size != sizeof(DWORD))
        return false;
    *value = data;
    return true;
}

bool ReadBool(HKEY key, const wchar_t* name, bool defaultValue)
{
    DWORD value = 0;
    if (!ReadDword(key, name, &value))
        return defaultValue;
    return value != 0;
}

int ReadInt(HKEY key, const wchar_t* name, int defaultValue)
{
    DWORD value = 0;
    if (!ReadDword(key, name, &value))
        return defaultValue;
    return static_cast<int>(value);
}

void WriteDword(HKEY key, const wchar_t* name, DWORD value)
{
    RegSetValueExW(key, name, 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

void WriteBool(HKEY key, const wchar_t* name, bool value)
{
    WriteDword(key, name, value ? 1 : 0);
}

void WriteInt(HKEY key, const wchar_t* name, int value)
{
    WriteDword(key, name, static_cast<DWORD>(value));
}

int ClampInt(int value, int minimum, int maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

bool DeleteValue(HKEY key, const wchar_t* name)
{
    return RegDeleteValueW(key, name) == ERROR_SUCCESS;
}
} // namespace

void ValidateSettings(AppSettings& settings)
{
    settings.dateFormat = ClampInt(settings.dateFormat, 0, ISLAND_DATE_COUNT - 1);
    settings.theme = ClampInt(settings.theme, ISLAND_THEME_DARK, ISLAND_THEME_SYSTEM);
    settings.windowSize = ClampInt(settings.windowSize, 0, ISLAND_SIZE_COUNT - 1);
    settings.position = ClampInt(settings.position, ISLAND_POS_LEFT, ISLAND_POS_CUSTOM);
    settings.fontSize = ClampInt(settings.fontSize, kFontSizeMin, kFontSizeMax);
    settings.opacity = ClampInt(settings.opacity, kOpacityMin, kOpacityMax);
    settings.hideDelaySeconds =
        ClampInt(settings.hideDelaySeconds, kHideDelayMinSeconds, kHideDelayMaxSeconds);
    settings.notifyPeekSeconds = ClampInt(settings.notifyPeekSeconds,
                                         kNotifyPeekSecondsMin, kNotifyPeekSecondsMax);

    if (settings.customX < -32000)
        settings.customX = -100000;
    if (settings.customY < -32000)
        settings.customY = -100000;

    // The 12-hour suffix is meaningless when the clock is in 24-hour mode, but
    // keep the stored preference so it comes back when the user switches back.
}

void LoadSettings()
{
    AppSettings settings; // defaults from app_state.h

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsRegistryKey, 0, KEY_READ, &key) ==
        ERROR_SUCCESS)
    {
        const DWORD version = static_cast<DWORD>(ReadInt(key, L"SettingsVersion", 0));

        settings.use24Hour = ReadBool(key, L"Use24Hour", settings.use24Hour);
        settings.showSeconds = ReadBool(key, L"ShowSeconds", settings.showSeconds);
        settings.showAmPm = ReadBool(key, L"ShowAmPm", settings.showAmPm);
        settings.showDate = ReadBool(key, L"ShowDate", settings.showDate);
        settings.showYear = ReadBool(key, L"ShowYear", settings.showYear);
        settings.showWeekday = ReadBool(key, L"ShowWeekday", settings.showWeekday);
        settings.dateFormat = ReadInt(key, L"DateFormat", settings.dateFormat);
        settings.theme = ReadInt(key, L"Theme", settings.theme);
        settings.windowSize = ReadInt(key, L"WindowSize", settings.windowSize);
        settings.position = ReadInt(key, L"Position", settings.position);
        settings.customX = ReadInt(key, L"CustomX", settings.customX);
        settings.customY = ReadInt(key, L"CustomY", settings.customY);
        settings.autoHide = ReadBool(key, L"AutoHide", settings.autoHide);
        settings.hideOnFullscreen =
            ReadBool(key, L"HideOnFullscreen", settings.hideOnFullscreen);
        settings.fontSize = ReadInt(key, L"FontSize", settings.fontSize);
        settings.boldFont = ReadBool(key, L"BoldFont", settings.boldFont);
        settings.opacity = ReadInt(key, L"Opacity", settings.opacity);
        settings.notifyEnable = ReadBool(key, L"NotifyEnable", settings.notifyEnable);
        settings.notifyShowContent =
            ReadBool(key, L"NotifyShowContent", settings.notifyShowContent);
        settings.notifyWeChat = ReadBool(key, L"NotifyWeChat", settings.notifyWeChat);
        settings.notifyQQ = ReadBool(key, L"NotifyQQ", settings.notifyQQ);
        settings.notifyPeekSeconds =
            ReadInt(key, L"NotifyPeekSeconds", settings.notifyPeekSeconds);

        if (version >= 2)
        {
            settings.hideDelaySeconds =
                ReadInt(key, L"HideDelaySeconds", settings.hideDelaySeconds);
        }
        else
        {
            // Migration from the pre-5.0 pair HideDelayMinutes + HideDelaySeconds.
            const int legacyMinutes = ReadInt(key, L"HideDelayMinutes", 0);
            const int legacySeconds = ReadInt(key, L"HideDelaySeconds", 1);
            settings.hideDelaySeconds =
                legacyMinutes * 60 + ClampInt(legacySeconds, 0, 59);
        }

        RegCloseKey(key);
    }

    ValidateSettings(settings);
    g_settings = settings;

    // The auto-start entry is the source of truth, not a stored flag.
    g_settings.autoStart = IsAutoStartEnabled();
}

bool SaveSettings()
{
    ValidateSettings(g_settings);

    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsRegistryKey, 0, nullptr,
                                  REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, nullptr,
                                  &key, nullptr);
    if (result != ERROR_SUCCESS || key == nullptr)
        return false;

    WriteDword(key, L"SettingsVersion", kSettingsVersion);
    WriteBool(key, L"Use24Hour", g_settings.use24Hour);
    WriteBool(key, L"ShowSeconds", g_settings.showSeconds);
    WriteBool(key, L"ShowAmPm", g_settings.showAmPm);
    WriteBool(key, L"ShowDate", g_settings.showDate);
    WriteBool(key, L"ShowYear", g_settings.showYear);
    WriteBool(key, L"ShowWeekday", g_settings.showWeekday);
    WriteInt(key, L"DateFormat", g_settings.dateFormat);
    WriteInt(key, L"Theme", g_settings.theme);
    WriteInt(key, L"WindowSize", g_settings.windowSize);
    WriteInt(key, L"Position", g_settings.position);
    WriteInt(key, L"CustomX", g_settings.customX);
    WriteInt(key, L"CustomY", g_settings.customY);
    WriteBool(key, L"AutoHide", g_settings.autoHide);
    WriteInt(key, L"HideDelaySeconds", g_settings.hideDelaySeconds);
    WriteBool(key, L"HideOnFullscreen", g_settings.hideOnFullscreen);
    WriteInt(key, L"FontSize", g_settings.fontSize);
    WriteBool(key, L"BoldFont", g_settings.boldFont);
    WriteInt(key, L"Opacity", g_settings.opacity);
    WriteBool(key, L"NotifyEnable", g_settings.notifyEnable);
    WriteBool(key, L"NotifyShowContent", g_settings.notifyShowContent);
    WriteBool(key, L"NotifyWeChat", g_settings.notifyWeChat);
    WriteBool(key, L"NotifyQQ", g_settings.notifyQQ);
    WriteInt(key, L"NotifyPeekSeconds", g_settings.notifyPeekSeconds);

    // Legacy values are no longer used once the new schema has been written.
    DeleteValue(key, L"HideDelayMinutes");

    RegCloseKey(key);

    SetAutoStartEnabled(g_settings.autoStart);
    return true;
}

bool IsAutoStartEnabled()
{
    wchar_t value[MAX_PATH * 2] = {};
    DWORD size = sizeof(value);
    DWORD type = 0;
    const LONG result = RegGetValueW(HKEY_CURRENT_USER, kRunRegistryKey, L"DynamicIsland",
                                     RRF_RT_REG_SZ, &type, value, &size);
    return result == ERROR_SUCCESS;
}

void SetAutoStartEnabled(bool enabled)
{
    if (enabled)
    {
        wchar_t modulePath[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return;

        std::wstring command = L"\"";
        command += modulePath;
        command += L"\"";

        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunRegistryKey, 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key,
                            nullptr) == ERROR_SUCCESS)
        {
            RegSetValueExW(key, L"DynamicIsland", 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(command.c_str()),
                           static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(key);
        }
    }
    else
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunRegistryKey, 0, KEY_SET_VALUE, &key) ==
            ERROR_SUCCESS)
        {
            RegDeleteValueW(key, L"DynamicIsland");
            RegCloseKey(key);
        }
    }
}
