#pragma once

#include <windows.h>

// The animation module owns the eased hover expansion and the hide/show slide.
void InitIslandAnimation(HWND hwnd);

// Request target states; the module starts its own 16 ms timer when needed.
void SetIslandHoverTarget(bool hovering);
void SetIslandHiddenTarget(bool hidden);

// Circular message sub-islands (index: 0 = WeChat, 1 = QQ). Each has its own
// independent pop-in / pop-out animation.
void SetSubIslandTarget(int sourceIndex, bool visible);
bool IsSubIslandTarget(int sourceIndex);
float GetSubIslandProgress(int sourceIndex);
bool IsAnySubIslandVisible();

bool IsIslandHoverTarget();
bool IsIslandHiddenTarget();
bool IsIslandAnimationRunning();

// Eased progress values in [0, 1] used by the renderer.
float GetIslandHoverProgress();
float GetIslandHideProgress();

// Advance the animation; called from the kTimerAnimation handler.
void UpdateIslandAnimation();

// Jump straight to the target states (used after display / DPI changes).
void FinishIslandAnimation();

// Stop the internal timer without changing the target values.
void StopIslandAnimationTimer();
