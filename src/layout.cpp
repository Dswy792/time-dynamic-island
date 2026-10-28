#include "layout.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

#include "app_state.h"

namespace
{
// Capsule sizes in device-independent pixels (96 DPI).
constexpr int kBaseWidthDip[ISLAND_SIZE_COUNT] = {120, 150, 180};
constexpr int kBaseHeightDip[ISLAND_SIZE_COUNT] = {24, 28, 32};

// Transparent padding inside the layered window that holds the soft shadow.
constexpr int kShadowHorizontalDip = 16;
constexpr int kShadowTopDip = 10;
constexpr int kShadowBottomDip = 20;

constexpr int kTopMarginDip = 6;
constexpr int kSideMarginDip = 12;
constexpr int kTriggerPaddingDip = 10;
constexpr float kHoverScale = 1.08f;

HWND s_hwnd = nullptr;
UINT s_dpi = 96;

UINT QueryWindowDpi(HWND hwnd)
{
    typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn function = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (function != nullptr && hwnd != nullptr)
    {
        const UINT dpi = function(hwnd);
        if (dpi != 0)
            return dpi;
    }

    UINT dpi = 96;
    HDC dc = GetDC(nullptr);
    if (dc != nullptr)
    {
        dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX));
        ReleaseDC(nullptr, dc);
    }
    return dpi == 0 ? 96 : dpi;
}

int ClampValue(int value, int minimum, int maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

RECT MakeRect(int left, int top, int width, int height)
{
    RECT rect = {left, top, left + width, top + height};
    return rect;
}

bool HasCustomPosition()
{
    return g_settings.position == ISLAND_POS_CUSTOM && g_settings.customX > -32000 &&
           g_settings.customY > -32000;
}

RECT GetMonitorWorkAreaForPoint(POINT point)
{
    HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info))
        return info.rcWork;

    RECT fallback = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &fallback, 0);
    return fallback;
}
} // namespace

void InitIslandLayout(HWND hwnd)
{
    s_hwnd = hwnd;
    s_dpi = QueryWindowDpi(hwnd);
    if (s_dpi == 0)
        s_dpi = 96;
}

UINT GetIslandDpi()
{
    return s_dpi;
}

int ScaleDipForDpi(int dip, UINT dpi)
{
    return MulDiv(dip, static_cast<int>(dpi), 96);
}

int ScaleDip(int dip)
{
    return ScaleDipForDpi(dip, s_dpi);
}

RECT GetIslandWorkArea()
{
    RECT workArea = {};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0) ||
        workArea.right <= workArea.left || workArea.bottom <= workArea.top)
    {
        workArea.left = 0;
        workArea.top = 0;
        workArea.right = GetSystemMetrics(SM_CXSCREEN);
        workArea.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    return workArea;
}

RECT GetIslandRestingCapsuleRect()
{
    const int widthDip = kBaseWidthDip[ClampValue(g_settings.windowSize, 0, ISLAND_SIZE_COUNT - 1)];
    const int heightDip =
        kBaseHeightDip[ClampValue(g_settings.windowSize, 0, ISLAND_SIZE_COUNT - 1)];
    const int width = ScaleDip(widthDip);
    const int height = ScaleDip(heightDip);

    int left = 0;
    int top = 0;

    if (HasCustomPosition())
    {
        POINT point = {g_settings.customX, g_settings.customY};
        const RECT workArea = GetMonitorWorkAreaForPoint(point);
        left = ClampValue(g_settings.customX, workArea.left, workArea.right - width);
        top = ClampValue(g_settings.customY, workArea.top, workArea.bottom - height);
    }
    else
    {
        const RECT workArea = GetIslandWorkArea();
        const int available = workArea.right - workArea.left;
        switch (g_settings.position)
        {
        case ISLAND_POS_LEFT:
            left = workArea.left + ScaleDip(kSideMarginDip);
            break;
        case ISLAND_POS_RIGHT:
            left = workArea.right - width - ScaleDip(kSideMarginDip);
            break;
        default:
            left = workArea.left + (available - width) / 2;
            break;
        }
        top = workArea.top + ScaleDip(kTopMarginDip);
    }

    return MakeRect(left, top, width, height);
}

IslandGeometry ComputeIslandGeometry(float hoverProgress, float hideProgress)
{
    const RECT resting = GetIslandRestingCapsuleRect();
    const RECT workArea = GetIslandWorkArea();

    hoverProgress = std::max(0.0f, std::min(1.0f, hoverProgress));
    hideProgress = std::max(0.0f, std::min(1.0f, hideProgress));

    const float scale = 1.0f + (kHoverScale - 1.0f) * hoverProgress;
    const int restingWidth = resting.right - resting.left;
    const int restingHeight = resting.bottom - resting.top;
    const int capsuleWidth = std::max(1, static_cast<int>(std::lround(restingWidth * scale)));
    const int capsuleHeight = std::max(1, static_cast<int>(std::lround(restingHeight * scale)));
    const int centerX = (resting.left + resting.right) / 2;

    // Distance required to move the capsule (and its shadow) completely above
    // the work area.
    const int hiddenDistance = (resting.top - workArea.top) + capsuleHeight +
                               ScaleDip(kShadowBottomDip) + ScaleDip(4);
    const int capsuleTop =
        resting.top - static_cast<int>(std::lround(hiddenDistance * hideProgress));

    IslandGeometry geometry = {};
    geometry.capsule = MakeRect(centerX - capsuleWidth / 2, capsuleTop, capsuleWidth,
                                capsuleHeight);
    geometry.window = MakeRect(geometry.capsule.left - ScaleDip(kShadowHorizontalDip),
                               geometry.capsule.top - ScaleDip(kShadowTopDip),
                               capsuleWidth + ScaleDip(kShadowHorizontalDip) * 2,
                               capsuleHeight + ScaleDip(kShadowTopDip) +
                                   ScaleDip(kShadowBottomDip));
    geometry.dpi = static_cast<int>(s_dpi);
    return geometry;
}

RECT GetIslandTriggerRect()
{
    RECT trigger = GetIslandRestingCapsuleRect();
    const int padding = ScaleDip(kTriggerPaddingDip);
    InflateRect(&trigger, padding, padding);

    const RECT workArea = GetIslandWorkArea();
    if (trigger.top > workArea.top)
        trigger.top = workArea.top;
    return trigger;
}

void ApplyIslandWindowGeometry(HWND hwnd, const IslandGeometry& geometry)
{
    if (hwnd == nullptr)
        return;

    SetWindowPos(hwnd, HWND_TOPMOST, geometry.window.left, geometry.window.top,
                 geometry.window.right - geometry.window.left,
                 geometry.window.bottom - geometry.window.top,
                 SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOSENDCHANGING);
}

void ClearCustomPosition()
{
    g_settings.position = ISLAND_POS_CENTER;
    g_settings.customX = -100000;
    g_settings.customY = -100000;
}

bool PointInRect(const RECT& rect, POINT pt)
{
    return pt.x >= rect.left && pt.x < rect.right && pt.y >= rect.top &&
           pt.y < rect.bottom;
}
