#pragma once

#include <windows.h>
#include <gdiplus.h>

#include <string>

#include "app_state.h"
#include "theme.h"

// Per-pixel alpha layered renderer (GDI+ -> premultiplied DIB -> UpdateLayeredWindow).
void InitIslandRenderer(HWND hwnd);
void ShutdownIslandRenderer();

// Render the current frame. When force is false the frame is skipped if the
// visible content and geometry did not change.
void RenderIslandFrame(bool force);

// Mark the content as stale (time / theme / settings changed).
void InvalidateIslandContent();

// Latest message sender / body per sub-island source index (0 = WeChat, 1 = QQ).
// Used by the hover tooltip when "show content" is enabled.
void SetSubIslandContent(int sourceIndex, const std::wstring& title,
                         const std::wstring& body);

// Index of the sub-island currently under the mouse (-1 = none). Controls the
// hover ring and the message tooltip.
void SetHoveredSubIsland(int sourceIndex);

// Draws exactly the same capsule (shadow, gradient, border, fitted clock text)
// as the live island, at its real DPI-scaled size, centred in bounds. Used by
// the settings window preview so it matches the real thing pixel for pixel.
void RenderIslandPreview(Gdiplus::Graphics& graphics, const Gdiplus::RectF& bounds,
                         UINT dpi, const AppSettings& settings,
                         const ThemePalette& palette);
