#pragma once

#include <windows.h>
#include <gdiplus.h>

// Convert a COLORREF + alpha into a GDI+ color (non-premultiplied).
inline Gdiplus::Color GdiColor(COLORREF color, BYTE alpha = 255)
{
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

// Blend two colors, weightB in 0..255.
inline COLORREF BlendColor(COLORREF a, COLORREF b, int weightB)
{
    if (weightB <= 0)
        return a;
    if (weightB >= 255)
        return b;
    const int wa = 255 - weightB;
    return RGB((GetRValue(a) * wa + GetRValue(b) * weightB) / 255,
               (GetGValue(a) * wa + GetGValue(b) * weightB) / 255,
               (GetBValue(a) * wa + GetBValue(b) * weightB) / 255);
}

// Scale a color towards black/white by a percentage (100 = unchanged).
inline COLORREF ScaleColor(COLORREF color, int percent)
{
    int r = GetRValue(color) * percent / 100;
    int g = GetGValue(color) * percent / 100;
    int b = GetBValue(color) * percent / 100;
    r = r > 255 ? 255 : (r < 0 ? 0 : r);
    g = g > 255 ? 255 : (g < 0 ? 0 : g);
    b = b > 255 ? 255 : (b < 0 ? 0 : b);
    return RGB(r, g, b);
}
