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

// Compute the geometry for the given (eased) animation progress values.
// hoverProgress: 0 = normal size, 1 = fully expanded
// hideProgress:  0 = visible, 1 = fully hidden above the screen
IslandGeometry ComputeIslandGeometry(float hoverProgress, float hideProgress);

// Area used to detect the mouse for auto-hide / reveal (resting capsule plus
// a small padding so the user does not need pixel precision).
RECT GetIslandTriggerRect();

// Move the layered window so that it matches the given geometry.
void ApplyIslandWindowGeometry(HWND hwnd, const IslandGeometry& geometry);

// Forget the custom (dragged) position; the next layout uses the preset.
void ClearCustomPosition();

// True when the point is inside the rectangle.
bool PointInRect(const RECT& rect, POINT pt);
