#pragma once

#include <windows.h>

// Resolved colors for both the island window and the settings panel.
struct ThemePalette
{
    bool dark;

    COLORREF islandBackground;
    COLORREF islandText;
    COLORREF islandBorder;

    COLORREF panelBackground;
    COLORREF panelSurface;
    COLORREF panelSurfaceHover;
    COLORREF panelText;
    COLORREF panelTextSecondary;
    COLORREF panelBorder;
    COLORREF accent;
    COLORREF accentText;
};

// Reads AppsUseLightTheme once and caches the result.
bool IsSystemThemeLight();

// Invalidate the cached system theme value (call on WM_THEMECHANGED /
// WM_SETTINGCHANGE / ImmersiveColorSet notifications).
void RefreshSystemThemeCache();

// Resolve the palette for an arbitrary theme value (ISLAND_THEME_DARK /
// ISLAND_THEME_LIGHT / ISLAND_THEME_SYSTEM). The returned reference stays
// valid for the lifetime of the process.
const ThemePalette& GetThemePaletteFor(int theme);

// Resolve the palette for the current g_settings.theme value.
const ThemePalette& GetThemePalette();
