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

namespace
{
RECT UnionRectCopy(const RECT& a, const RECT& b)
{
    if (a.right <= a.left || a.bottom <= a.top)
        return b;
    if (b.right <= b.left || b.bottom <= b.top)
        return a;
    RECT result = a;
    result.left = std::min(a.left, b.left);
    result.top = std::min(a.top, b.top);
    result.right = std::max(a.right, b.right);
    result.bottom = std::max(a.bottom, b.bottom);
    return result;
}

bool IsEmptyRect(const RECT& rect)
{
    return rect.right <= rect.left || rect.bottom <= rect.top;
}
} // namespace

IslandClusterGeometry ComputeIslandClusterGeometry(
    float hoverProgress, float hideProgress,
    const float subProgress[kSubIslandSourceCount], int hoveredSub,
    bool tooltipVisible)
{
    const RECT resting = GetIslandRestingCapsuleRect();
    const RECT workArea = GetIslandWorkArea();

    hoverProgress = std::max(0.0f, std::min(1.0f, hoverProgress));
    hideProgress = std::max(0.0f, std::min(1.0f, hideProgress));

    const float scale = 1.0f + (kHoverScale - 1.0f) * hoverProgress;
    const int restingWidth = resting.right - resting.left;
    const int restingHeight = resting.bottom - resting.top;
    const int capsuleWidth =
        std::max(1, static_cast<int>(std::lround(restingWidth * scale)));
    const int capsuleHeight =
        std::max(1, static_cast<int>(std::lround(restingHeight * scale)));
    const int centerX = (resting.left + resting.right) / 2;

    const int circleDiameter = capsuleHeight;
    const int gap = ScaleDip(kSubIslandGapDip);

    // Cluster width = main capsule + every visible sub-island (scaled by its
    // own progress). The whole cluster is centred on the resting centre.
    float clusterWidth = static_cast<float>(capsuleWidth);
    float progress[kSubIslandSourceCount] = {};
    for (int i = 0; i < kSubIslandSourceCount; ++i)
    {
        progress[i] = std::max(0.0f, std::min(1.0f, subProgress[i]));
        if (progress[i] > 0.001f)
            clusterWidth += gap * progress[i] +
                            static_cast<float>(circleDiameter) * progress[i];
    }

    const int hiddenDistance = (resting.top - workArea.top) + capsuleHeight +
                               ScaleDip(kShadowBottomDip) + ScaleDip(4);
    const int capsuleTop =
        resting.top - static_cast<int>(std::lround(hiddenDistance * hideProgress));
    const int capsuleCenterY = capsuleTop + capsuleHeight / 2;
    const int clusterLeft =
        centerX - static_cast<int>(std::lround(clusterWidth / 2.0f));

    IslandClusterGeometry geometry = {};
    geometry.dpi = static_cast<int>(s_dpi);
    geometry.capsule =
        MakeRect(clusterLeft, capsuleTop, capsuleWidth, capsuleHeight);

    float cursorX = static_cast<float>(geometry.capsule.right);
    for (int i = 0; i < kSubIslandSourceCount; ++i)
    {
        if (progress[i] <= 0.001f)
            continue;
        const float diameter = static_cast<float>(circleDiameter) * progress[i];
        const float gapWidth = static_cast<float>(gap) * progress[i];
        const float circleCenterX = cursorX + gapWidth + diameter / 2.0f;
        const int radius = static_cast<int>(std::lround(diameter / 2.0f));
        const int centerY = capsuleCenterY;
        geometry.subIslands[i] = MakeRect(
            static_cast<int>(std::lround(circleCenterX)) - radius,
            centerY - radius, radius * 2, radius * 2);
        cursorX += gapWidth + diameter;
    }

    // Window = union of the shadow bands around every element.
    RECT window = MakeRect(geometry.capsule.left - ScaleDip(kShadowHorizontalDip),
                           geometry.capsule.top - ScaleDip(kShadowTopDip),
                           capsuleWidth + ScaleDip(kShadowHorizontalDip) * 2,
                           capsuleHeight + ScaleDip(kShadowTopDip) +
                               ScaleDip(kShadowBottomDip));
    for (int i = 0; i < kSubIslandSourceCount; ++i)
    {
        if (IsEmptyRect(geometry.subIslands[i]))
            continue;
        RECT area = geometry.subIslands[i];
        InflateRect(&area, ScaleDip(10), ScaleDip(10));
        area.top += ScaleDip(2);
        area.bottom += ScaleDip(6);
        window = UnionRectCopy(window, area);
    }

    // Message tooltip below the hovered sub-island (or below the main capsule
    // when only the clock is hovered - not used today).
    if (tooltipVisible && hoveredSub >= 0 && hoveredSub < kSubIslandSourceCount &&
        !IsEmptyRect(geometry.subIslands[hoveredSub]))
    {
        const int tooltipWidth = ScaleDip(kSubIslandTooltipWidthDip);
        const int tooltipHeight = ScaleDip(kSubIslandTooltipHeightDip);
        const int centerXTooltip = (geometry.subIslands[hoveredSub].left +
                                    geometry.subIslands[hoveredSub].right) / 2;
        int left = centerXTooltip - tooltipWidth / 2;
        const int top = geometry.capsule.bottom + ScaleDip(kSubIslandGapDip);
        // Keep the tooltip attached to the window; let the window grow instead.
        geometry.tooltip = MakeRect(left, top, tooltipWidth, tooltipHeight);
        RECT area = geometry.tooltip;
        InflateRect(&area, ScaleDip(12), ScaleDip(6));
        area.bottom += ScaleDip(10);
        window = UnionRectCopy(window, area);
    }

    // Keep the whole composition inside the monitor work area horizontally
    // (a custom position near an edge must not push the message circles off
    // screen). The visual centre rule still applies for normal positions.
    int shiftX = 0;
    if (window.right > workArea.right)
        shiftX = static_cast<int>(workArea.right) - window.right;
    if (window.left + shiftX < workArea.left)
        shiftX = static_cast<int>(workArea.left) - window.left;
    if (shiftX != 0)
    {
        OffsetRect(&geometry.capsule, shiftX, 0);
        for (int i = 0; i < kSubIslandSourceCount; ++i)
        {
            if (!IsEmptyRect(geometry.subIslands[i]))
                OffsetRect(&geometry.subIslands[i], shiftX, 0);
        }
        if (!IsEmptyRect(geometry.tooltip))
            OffsetRect(&geometry.tooltip, shiftX, 0);
        OffsetRect(&window, shiftX, 0);
    }

    geometry.window = window;
    return geometry;
}

RECT GetIslandClusterTriggerRect()
{
    RECT trigger = GetIslandRestingCapsuleRect();
    const int padding = ScaleDip(kTriggerPaddingDip);
    InflateRect(&trigger, padding, padding);

    // Reserve room for two sub-islands on the right and the tooltip below.
    const int restingHeightPx = trigger.bottom - trigger.top - 2 * padding;
    const int circleDiameter = restingHeightPx;
    const int gap = ScaleDip(kSubIslandGapDip);
    trigger.right +=
        (gap + circleDiameter) * kSubIslandSourceCount + ScaleDip(10);
    trigger.bottom += ScaleDip(kSubIslandTooltipHeightDip) + ScaleDip(20);

    const RECT workArea = GetIslandWorkArea();
    if (trigger.top > workArea.top)
        trigger.top = workArea.top;
    if (trigger.right > workArea.right)
        trigger.right = workArea.right;
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
