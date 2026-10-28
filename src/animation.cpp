#include "animation.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

#include "app_state.h"
#include "rendering.h"

namespace
{
constexpr float kHoverDurationMs = 170.0f;
constexpr float kHideDurationMs = 260.0f;

HWND s_hwnd = nullptr;
bool s_hoverTarget = false;
bool s_hiddenTarget = false;
float s_hover = 0.0f;
float s_hide = 0.0f;
ULONGLONG s_lastTick = 0;
bool s_timerRunning = false;

float EaseOutCubic(float value)
{
    const float inverse = 1.0f - value;
    return 1.0f - inverse * inverse * inverse;
}

float EaseInOutCubic(float value)
{
    if (value < 0.5f)
        return 4.0f * value * value * value;
    const float shift = -2.0f * value + 2.0f;
    return 1.0f - (shift * shift * shift) / 2.0f;
}

float MoveTowards(float current, float target, float maximumDelta)
{
    if (current < target)
        return std::min(target, current + maximumDelta);
    if (current > target)
        return std::max(target, current - maximumDelta);
    return target;
}

void StartTimer()
{
    if (s_timerRunning || s_hwnd == nullptr)
        return;
    s_lastTick = GetTickCount64();
    SetTimer(s_hwnd, kTimerAnimation, kAnimationTickMs, nullptr);
    s_timerRunning = true;
}

void StopTimer()
{
    if (!s_timerRunning || s_hwnd == nullptr)
        return;
    KillTimer(s_hwnd, kTimerAnimation);
    s_timerRunning = false;
}
} // namespace

void InitIslandAnimation(HWND hwnd)
{
    s_hwnd = hwnd;
    s_hoverTarget = false;
    s_hiddenTarget = false;
    s_hover = 0.0f;
    s_hide = 0.0f;
    s_timerRunning = false;
}

void SetIslandHoverTarget(bool hovering)
{
    if (s_hoverTarget == hovering)
        return;
    s_hoverTarget = hovering;
    StartTimer();
}

void SetIslandHiddenTarget(bool hidden)
{
    if (s_hiddenTarget == hidden)
        return;
    s_hiddenTarget = hidden;
    StartTimer();
}

bool IsIslandHoverTarget()
{
    return s_hoverTarget;
}

bool IsIslandHiddenTarget()
{
    return s_hiddenTarget;
}

bool IsIslandAnimationRunning()
{
    return s_timerRunning;
}

float GetIslandHoverProgress()
{
    return EaseOutCubic(s_hover);
}

float GetIslandHideProgress()
{
    return EaseInOutCubic(s_hide);
}

void UpdateIslandAnimation()
{
    if (s_hwnd == nullptr)
        return;

    const ULONGLONG now = GetTickCount64();
    float deltaMs = static_cast<float>(now - s_lastTick);
    s_lastTick = now;
    if (deltaMs < 0.0f)
        deltaMs = 0.0f;
    if (deltaMs > 100.0f)
        deltaMs = 100.0f;

    const float hoverStep = deltaMs / kHoverDurationMs;
    const float hideStep = deltaMs / kHideDurationMs;

    s_hover = MoveTowards(s_hover, s_hoverTarget ? 1.0f : 0.0f, hoverStep);
    s_hide = MoveTowards(s_hide, s_hiddenTarget ? 1.0f : 0.0f, hideStep);

    const bool settled = s_hover == (s_hoverTarget ? 1.0f : 0.0f) &&
                         s_hide == (s_hiddenTarget ? 1.0f : 0.0f);
    if (settled)
        StopTimer();

    RenderIslandFrame(false);
}

void FinishIslandAnimation()
{
    s_hover = s_hoverTarget ? 1.0f : 0.0f;
    s_hide = s_hiddenTarget ? 1.0f : 0.0f;
    StopTimer();
    RenderIslandFrame(true);
}

void StopIslandAnimationTimer()
{
    StopTimer();
}
