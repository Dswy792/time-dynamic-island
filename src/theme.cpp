#include "theme.h"

#include <windows.h>

#include "app_state.h"

namespace
{
const wchar_t kPersonalizeKey[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

const ThemePalette kDarkPalette = {
    true,                    // dark
    RGB(24, 24, 28),         // islandBackground
    RGB(245, 245, 250),      // islandText
    RGB(255, 255, 255),      // islandBorder
    RGB(28, 28, 33),         // panelBackground
    RGB(40, 40, 46),         // panelSurface
    RGB(52, 52, 60),         // panelSurfaceHover
    RGB(240, 240, 245),      // panelText
    RGB(150, 150, 160),      // panelTextSecondary
    RGB(60, 60, 68),         // panelBorder
    RGB(10, 132, 255),       // accent
    RGB(255, 255, 255),      // accentText
};

const ThemePalette kLightPalette = {
    false,                   // dark
    RGB(248, 248, 252),      // islandBackground
    RGB(22, 22, 26),         // islandText
    RGB(0, 0, 0),            // islandBorder
    RGB(246, 246, 250),      // panelBackground
    RGB(255, 255, 255),      // panelSurface
    RGB(236, 236, 242),      // panelSurfaceHover
    RGB(24, 24, 28),         // panelText
    RGB(110, 110, 120),      // panelTextSecondary
    RGB(220, 220, 228),      // panelBorder
    RGB(0, 122, 255),        // accent
    RGB(255, 255, 255),      // accentText
};

bool g_systemThemeCached = false;
bool g_systemThemeLight = true;

// Reads AppsUseLightTheme from the per-user personalization key. Windows writes
// the value after the shell has started; treat a missing value as the classic
// light theme.
bool ReadSystemThemeLight()
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LONG result =
        RegGetValueW(HKEY_CURRENT_USER, kPersonalizeKey, L"AppsUseLightTheme",
                     RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (result != ERROR_SUCCESS || size != sizeof(value))
        return true;
    return value != 0;
}
} // namespace

bool IsSystemThemeLight()
{
    if (!g_systemThemeCached)
    {
        g_systemThemeLight = ReadSystemThemeLight();
        g_systemThemeCached = true;
    }
    return g_systemThemeLight;
}

void RefreshSystemThemeCache()
{
    g_systemThemeCached = false;
    g_systemThemeLight = true;
}

const ThemePalette& GetThemePaletteFor(int theme)
{
    switch (theme)
    {
    case ISLAND_THEME_LIGHT:
        return kLightPalette;

    case ISLAND_THEME_SYSTEM:
        return IsSystemThemeLight() ? kLightPalette : kDarkPalette;

    case ISLAND_THEME_DARK:
    default:
        return kDarkPalette;
    }
}

const ThemePalette& GetThemePalette()
{
    return GetThemePaletteFor(g_settings.theme);
}
