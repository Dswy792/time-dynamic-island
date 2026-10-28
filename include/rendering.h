#pragma once

#include <windows.h>

// Per-pixel alpha layered renderer (GDI+ -> premultiplied DIB -> UpdateLayeredWindow).
void InitIslandRenderer(HWND hwnd);
void ShutdownIslandRenderer();

// Render the current frame. When force is false the frame is skipped if the
// visible content and geometry did not change.
void RenderIslandFrame(bool force);

// Mark the content as stale (time / theme / settings changed).
void InvalidateIslandContent();
