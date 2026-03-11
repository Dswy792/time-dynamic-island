#include "rendering.h"

#include <string>

#include "app_state.h"
#include "time_theme.h"

void RenderWindow()
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(g_hwnd, &ps);

    if (hdc == NULL)
        return;

    RECT clientRect;
    GetClientRect(g_hwnd, &clientRect);

    COLORREF bgColor = GetBackgroundColor();
    COLORREF textColor = GetTextColor();

    HBRUSH bgBrush = CreateSolidBrush(bgColor);
    FillRect(hdc, &clientRect, bgBrush);
    DeleteObject(bgBrush);

    std::wstring dateStr = g_showDate ? GetDateString() : L"";
    std::wstring timeStr = GetTimeString();
    std::wstring displayStr = dateStr + L"  " + timeStr;

    int fontWeight = g_boldFont ? FW_BOLD : FW_NORMAL;
    HFONT font = CreateFontW(
        -g_fontSize,
        0, 0, 0, fontWeight, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei UI");

    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SetTextColor(hdc, textColor);
    SetBkMode(hdc, TRANSPARENT);
    SetTextAlign(hdc, TA_CENTER | TA_BASELINE);

    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    int centerX = clientRect.right / 2;
    int centerY = (clientRect.bottom + tm.tmAscent - tm.tmDescent) / 2;
    TextOutW(hdc, centerX, centerY, displayStr.c_str(), (int)displayStr.length());

    SelectObject(hdc, oldFont);
    DeleteObject(font);

    EndPaint(g_hwnd, &ps);
}

void CALLBACK TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    UNREFERENCED_PARAMETER(uMsg);
    UNREFERENCED_PARAMETER(dwTime);

    if (idEvent == 1)
    {
        InvalidateRect(hwnd, NULL, FALSE);
        UpdateWindow(hwnd);
    }
}
