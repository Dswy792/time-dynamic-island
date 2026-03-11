#include "animation.h"

#include <windows.h>

#include "app_state.h"
#include "layout.h"

void CheckMousePositionAndShowHide()
{
    if (!g_autoHide || g_isAnimating)
        return;

    POINT pt;
    GetCursorPos(&pt);

    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);

    int baseWidth = SIZE_WIDTH[g_windowSize];
    int baseHeight = SIZE_HEIGHT[g_windowSize];
    int width = (int)(baseWidth * 1.3);
    int height = (int)(baseHeight * 1.3);

    int x, visibleY;
    switch (g_position)
    {
    case 0:
        x = workArea.left + 10;
        break;
    case 2:
        x = workArea.right - width - 10;
        break;
    default:
        x = workArea.left + (workArea.right - workArea.left - width) / 2;
        break;
    }
    visibleY = workArea.top + 10;

    int triggerPadding = 10;
    bool mouseInTriggerArea = (pt.x >= x - triggerPadding && pt.x <= x + width + triggerPadding &&
                               pt.y >= visibleY - triggerPadding && pt.y <= visibleY + height + triggerPadding);

    if (g_isHidden)
    {
        if (mouseInTriggerArea)
        {
            g_isHidden = false;
            g_isHovering = true;
            StartShowAnimation();
        }
        return;
    }

    if (mouseInTriggerArea)
    {
        if (g_waitingForHide)
        {
            KillTimer(g_hwnd, 2);
            g_waitingForHide = false;
        }
    }
    else
    {
        if (!g_waitingForHide)
        {
            int hideDelayMs = (g_hideDelayMinutes * 60 + g_hideDelaySeconds) * 1000;
            SetTimer(g_hwnd, 2, hideDelayMs, NULL);
            g_waitingForHide = true;
        }
    }
}

void StartHideAnimation()
{
    g_isAnimating = true;
    g_isHiding = true;
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    g_animationY = workArea.top + 10;
    SetTimer(g_hwnd, ANIMATION_TIMER_ID, ANIMATION_INTERVAL, NULL);
}

void StartShowAnimation()
{
    g_isAnimating = true;
    g_isHiding = false;
    g_animationY = g_hiddenY;
    SetTimer(g_hwnd, ANIMATION_TIMER_ID, ANIMATION_INTERVAL, NULL);
}

void UpdateAnimation()
{
    if (!g_isAnimating)
        return;

    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);

    if (g_isHiding)
    {
        g_animationY -= ANIMATION_STEP;
        if (g_animationY <= g_hiddenY)
        {
            g_animationY = g_hiddenY;
            g_isAnimating = false;
            KillTimer(g_hwnd, ANIMATION_TIMER_ID);
        }
    }
    else
    {
        g_animationY += ANIMATION_STEP;
        if (g_animationY >= workArea.top + 10)
        {
            g_animationY = workArea.top + 10;
            g_isAnimating = false;
            g_isHovering = true;
            KillTimer(g_hwnd, ANIMATION_TIMER_ID);
        }
    }

    UpdateWindowPositionWithY(g_animationY);
}
