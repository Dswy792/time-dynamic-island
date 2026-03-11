#include "layout.h"

#include <windows.h>

#include "app_state.h"

void ShowWindowAtPosition(int x, int y, int width, int height)
{
    g_currentX = x;
    g_currentY = y;
    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
}

void UpdateWindowPosition(bool expanded)
{
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    int screenWidth = workArea.right - workArea.left;

    int baseWidth = SIZE_WIDTH[g_windowSize];
    int baseHeight = SIZE_HEIGHT[g_windowSize];
    int width = expanded ? (int)(baseWidth * 1.3) : baseWidth;
    int height = expanded ? (int)(baseHeight * 1.3) : baseHeight;

    int x;
    switch (g_position)
    {
    case 0:
        x = workArea.left + 10;
        break;
    case 2:
        x = workArea.right - width - 10;
        break;
    default:
        x = workArea.left + (screenWidth - width) / 2;
        break;
    }

    int y = workArea.top + 10;

    if (g_autoHide)
    {
        y = g_isHovering ? y : g_hiddenY;
    }

    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);

    int radius = height / 2 * 15 / 10;
    HRGN hRgn = CreateRoundRectRgn(0, 0, width, height, radius, radius);
    SetWindowRgn(g_hwnd, hRgn, TRUE);
}

void UpdateWindowPositionWithY(int y)
{
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    int screenWidth = workArea.right - workArea.left;

    int baseWidth = SIZE_WIDTH[g_windowSize];
    int baseHeight = SIZE_HEIGHT[g_windowSize];
    int width = (int)(baseWidth * 1.3);
    int height = (int)(baseHeight * 1.3);

    int x;
    switch (g_position)
    {
    case 0:
        x = workArea.left + 10;
        break;
    case 2:
        x = workArea.right - width - 10;
        break;
    default:
        x = workArea.left + (screenWidth - width) / 2;
        break;
    }

    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);

    int radius = height / 2 * 15 / 10;
    HRGN hRgn = CreateRoundRectRgn(0, 0, width, height, radius, radius);
    SetWindowRgn(g_hwnd, hRgn, TRUE);
}
