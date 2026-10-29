#pragma once

#include <windows.h>

// Geometry of the island in screen pixels. The window is larger than the
// visible capsule because the shadow is drawn inside the layered window.
struct IslandGeometry
{
    RECT window;  // whole layered window (capsule + shadow padding)
    RECT capsule; // visible capsule
    int dpi;
};

// Store the island HWND and initialise DPI information.
void InitIslandLayout(HWND hwnd);

// Current DPI of the island window (96 = 100%).
UINT GetIslandDpi();

// Convert device-independent pixels to physical pixels using the island DPI.
int ScaleDip(int dip);
int ScaleDipForDpi(int dip, UINT dpi);

// Work area of the primary monitor, in physical pixels.
RECT GetIslandWorkArea();

// Where the capsule rests when it is fully visible and not hovered.
RECT GetIslandRestingCapsuleRect();

// --- v5.1 message sub-islands ------------------------------------------------
// Up to two circular sub-islands (WeChat = index 0, QQ = index 1) pop out on the
// right side of the main capsule. The whole cluster stays centred on the main
// capsule's resting centre.
constexpr int kSubIslandSourceCount = 2;
constexpr int kSubIslandGapDip = 8;       // gap between capsule / circles
constexpr int kSubIslandTooltipWidthDip = 300;
constexpr int kSubIslandTooltipHeightDip = 34;

struct IslandClusterGeometry
{
    RECT window;                              // whole layered window (shadow + tooltip)
    RECT capsule;                             // main clock capsule
    RECT subIslands[kSubIslandSourceCount];   // circle bounding boxes (empty = hidden)
    RECT tooltip;                             // message tooltip capsule (empty = hidden)
    int dpi;
};

// subProgress: eased 0..1 per source index (0 = WeChat, 1 = QQ)
// hoveredSub:  index of the hovered sub-island, or -1
// tooltipVisible: draw the message tooltip below the hovered sub-island
IslandClusterGeometry ComputeIslandClusterGeometry(
    float hoverProgress, float hideProgress,
    const float subProgress[kSubIslandSourceCount], int hoveredSub,
    bool tooltipVisible);

// Trigger area for auto-hide / reveal, including the space the sub-islands and
// the tooltip can occupy.
RECT GetIslandClusterTriggerRect();

// Area used to detect the mouse for auto-hide / reveal (resting capsule plus
// a small padding so the user does not need pixel precision).
RECT GetIslandTriggerRect();

// Move the layered window so that it matches the given geometry.
void ApplyIslandWindowGeometry(HWND hwnd, const IslandGeometry& geometry);

// Forget the custom (dragged) position; the next layout uses the preset.
void ClearCustomPosition();

// True when the point is inside the rectangle.
bool PointInRect(const RECT& rect, POINT pt);
