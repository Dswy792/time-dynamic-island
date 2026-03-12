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

    // 根据显示内容动态调整字体大小
    int actualFontSize = g_fontSize;
    if (g_showYear) {
        actualFontSize = g_fontSize - 2;  // 显示年份时减小 2 个字号
    }

    // 如果字体大小变化或字体未创建，重新创建字体
    if (g_hFont == nullptr) {
        int fontWeight = g_boldFont ? FW_BOLD : FW_NORMAL;
        g_hFont = CreateFontW(
            -actualFontSize,
            0, 0, 0, fontWeight, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Microsoft YaHei UI");
    }

    HFONT oldFont = (HFONT)SelectObject(hdc, g_hFont);
    SetTextColor(hdc, textColor);
    SetBkMode(hdc, TRANSPARENT);
    SetTextAlign(hdc, TA_CENTER | TA_BASELINE);

    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    int centerX = clientRect.right / 2;
    int centerY = (clientRect.bottom + tm.tmAscent - tm.tmDescent) / 2;
    TextOutW(hdc, centerX, centerY, displayStr.c_str(), (int)displayStr.length());

    SelectObject(hdc, oldFont);

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
