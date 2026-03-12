#include "animation.h"

#include <windows.h>

#include "app_state.h"
#include "layout.h"

/**
 * 检查鼠标位置并自动隐藏/显示窗口
 * 
 * 调用时机：Timer 3 每 100ms 触发一次
 * 
 * 工作流程：
 * 1. 如果未启用自动隐藏或正在动画中，直接返回
 * 2. 获取鼠标位置
 * 3. 定义触发区域（窗口尺寸的 1.3 倍）
 * 4. 计算触发区域的坐标
 * 5. 如果鼠标在触发区域内：
 *    - 取消隐藏等待状态
 *    - 销毁 Timer 2（隐藏延迟定时器）
 * 6. 如果鼠标不在触发区域内：
 *    - 如果未在等待隐藏，启动 Timer 2（延迟指定时间后隐藏）
 * 
 * 注意：此函数只在 g_autoHide 为 true 时工作
 */
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

    // 鼠标在触发区域内：取消隐藏等待
    if (mouseInTriggerArea)
    {
        if (g_waitingForHide)
        {
            KillTimer(g_hwnd, 2);  // 销毁隐藏延迟定时器
            g_waitingForHide = false;
        }
    }
    // 鼠标不在触发区域内：启动隐藏延迟定时器
    else
    {
        if (!g_waitingForHide)
        {
            KillTimer(g_hwnd, 2);  // 先销毁旧的定时器，防止重复
            int hideDelayMs = (g_hideDelayMinutes * 60 + g_hideDelaySeconds) * 1000;
            SetTimer(g_hwnd, 2, hideDelayMs, NULL);  // Timer 2: 延迟指定时间后隐藏
            g_waitingForHide = true;
        }
    }
}

/**
 * 启动隐藏动画
 * 
 * 调用时机：Timer 2 触发时（用户设置的延迟时间到）
 * 
 * 工作流程：
 * 1. 设置动画状态标志
 * 2. 设置正在隐藏标志
 * 3. 获取当前动画起始位置
 * 4. 启动 Timer 4（动画定时器，每 15ms 更新一次）
 * 
 * 后续流程：
 * - Timer 4 -> UpdateAnimation() -> 逐步更新窗口 Y 坐标
 * - 动画完成后 -> 停止 Timer 4
 */
void StartHideAnimation()
{
    g_isAnimating = true;
    g_isHiding = true;
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    g_animationY = workArea.top + 10;
    SetTimer(g_hwnd, ANIMATION_TIMER_ID, ANIMATION_INTERVAL, NULL);
}

/**
 * 启动显示动画
 * 
 * 调用时机：鼠标进入触发区域且窗口处于隐藏状态时
 * 
 * 工作流程：
 * 1. 设置动画状态标志
 * 2. 设置正在显示标志（g_isHiding = false）
 * 3. 设置动画起始位置为隐藏位置
 * 4. 启动 Timer 4（动画定时器，每 15ms 更新一次）
 * 
 * 后续流程：
 * - Timer 4 -> UpdateAnimation() -> 逐步更新窗口 Y 坐标到可见位置
 * - 动画完成后 -> 停止 Timer 4，设置 g_isHovering = true
 */
void StartShowAnimation()
{
    g_isAnimating = true;
    g_isHiding = false;
    g_animationY = g_hiddenY;
    SetTimer(g_hwnd, ANIMATION_TIMER_ID, ANIMATION_INTERVAL, NULL);
}

/**
 * 更新动画位置
 * 
 * 调用时机：Timer 4 每 15ms 触发一次
 * 
 * 工作流程：
 * 1. 如果未在动画中，直接返回
 * 2. 获取工作区域坐标
 * 3. 如果是隐藏动画：
 *    - Y 坐标向上移动（减小）
 *    - 到达隐藏位置后，停止动画，重置状态
 * 4. 如果是显示动画：
 *    - Y 坐标向下移动（增加）
 *    - 到达可见位置后，停止动画，重置状态，设置 g_isHidden = false
 * 5. 调用 UpdateWindowPositionWithY() 更新窗口位置
 * 
 * 动画参数：
 * - ANIMATION_STEP: 每帧移动的像素数
 * - ANIMATION_INTERVAL: 15ms (约 60fps)
 */
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
            g_isHiding = false;
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
            g_isHiding = false;
            g_isHidden = false;
            KillTimer(g_hwnd, ANIMATION_TIMER_ID);
        }
    }

    UpdateWindowPositionWithY(g_animationY);
}
