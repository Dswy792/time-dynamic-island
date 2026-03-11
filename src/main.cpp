// Dynamic Island - Capsule shape with settings dialog and auto-hide
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include "resource.h"

#include "actions.h"
#include "animation.h"
#include "app_state.h"
#include "layout.h"
#include "rendering.h"
#include "settings.h"
#include "time_theme.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")

// Window procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
        RenderWindow();
        return 0;

    case WM_TIMER:
        if (wParam == 1)
        {
            // Timer 1: Update time display (every second)
            InvalidateRect(hwnd, NULL, FALSE);
        }
        else if (wParam == 2)
        {
            // Timer 2: Auto-hide delay timer
            if (g_autoHide && g_waitingForHide && !g_isAnimating)
            {
                g_isHidden = true; // Mark as hidden
                g_waitingForHide = false;
                StartHideAnimation();
            }
        }
        else if (wParam == 3)
        {
            // Timer 3: Mouse position check (every 100ms)
            CheckMousePositionAndShowHide();
        }
        else if (wParam == 4)
        {
            // Timer 4: Animation timer
            UpdateAnimation();
        }
        return 0;

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rect;
        GetClientRect(hwnd, &rect);
        HBRUSH bgBrush = CreateSolidBrush(GetBackgroundColor());
        FillRect(hdc, &rect, bgBrush);
        DeleteObject(bgBrush);
        return TRUE;
    }

    case WM_MOUSEMOVE:
    {
        // Always handle hover expansion (regardless of auto-hide setting)
        if (!g_isHovering)
        {
            g_isHovering = true;
            UpdateWindowPosition(true); // Expand on hover

            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        g_isHovering = false;
        UpdateWindowPosition(false); // Shrink when not hovering
        // Timer 3 handles auto-hide logic
        return 0;
    }

    case WM_LBUTTONDOWN:
        OpenTimeSettings();
        return 0;

    case WM_RBUTTONDOWN:
        ShowSettingsDialog();
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        KillTimer(hwnd, 3);
        KillTimer(hwnd, 4);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Main function
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPWSTR lpCmdLine, int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Temporarily disable mutex check for testing
    // HANDLE hMutex = CreateMutexW(NULL, TRUE, L"DynamicIsland_Mutex");
    // if (GetLastError() == ERROR_ALREADY_EXISTS) {
    //     MessageBoxW(NULL, L"Dynamic Island is already running!", L"Info", MB_OK | MB_ICONINFORMATION);
    //     return 0;
    // }

    CoInitialize(NULL);
    SetAutoStartup();

    InitCommonControls();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"DynamicIslandClass";

    if (!RegisterClassExW(&wc))
    {
        MessageBoxW(NULL, L"Failed to register window class!", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);

    // Load settings first to get initial size and position
    LoadSettings();

    // Calculate initial position based on settings
    int baseWidth = SIZE_WIDTH[g_windowSize];
    int baseHeight = SIZE_HEIGHT[g_windowSize];
    int x;
    switch (g_position)
    {
    case 0: // Left
        x = workArea.left + (workArea.right - workArea.left - baseWidth) / 4;
        break;
    case 2: // Right
        x = workArea.left + (workArea.right - workArea.left - baseWidth) * 3 / 4;
        break;
    default: // Center
        x = workArea.left + (workArea.right - workArea.left - baseWidth) / 2;
        break;
    }
    int y = workArea.top + 10;

    g_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"DynamicIslandClass",
        L"DynamicIsland",
        WS_POPUP | WS_VISIBLE,
        x, y, baseWidth, baseHeight,
        NULL, NULL, hInstance, NULL);

    if (!g_hwnd)
    {
        DWORD error = GetLastError();
        wchar_t errorMsg[256];
        swprintf_s(errorMsg, 256, L"Failed to create window! Error code: %d", error);
        MessageBoxW(NULL, errorMsg, L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Create capsule shape region - 1.5x radius for more rounded corners
    int radius = (baseHeight / 2) * 15 / 10; // 1.5x radius
    HRGN hRgn = CreateRoundRectRgn(0, 0, baseWidth, baseHeight, radius, radius);
    SetWindowRgn(g_hwnd, hRgn, TRUE);

    // Show window
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    SetForegroundWindow(g_hwnd);

    // Timer for auto-update
    SetTimer(g_hwnd, 1, 1000, TimerProc);

    // Timer for mouse position check (auto-hide)
    SetTimer(g_hwnd, 3, MOUSE_CHECK_INTERVAL, NULL);

    // Force initial render
    InvalidateRect(g_hwnd, NULL, TRUE);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();

    return (int)msg.wParam;
}

// Windows entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    UNREFERENCED_PARAMETER(lpCmdLine);
    return wWinMain(hInstance, hPrevInstance, GetCommandLineW(), nCmdShow);
}
