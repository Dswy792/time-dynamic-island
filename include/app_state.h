#pragma once

#include <windows.h>

extern HWND g_hwnd;
extern HWND g_settingsDlg;

extern bool g_use24Hour;
extern bool g_showDate;
extern bool g_showYear;
extern bool g_showSeconds;
extern int g_theme;
extern bool g_autoHide;
extern bool g_isHovering;

extern int g_windowSize;
extern int g_position;
extern int g_hideDelayMinutes;
extern int g_hideDelaySeconds;

extern int g_fontSize;
extern bool g_boldFont;

extern const int SIZE_WIDTH[3];
extern const int SIZE_HEIGHT[3];

extern int g_currentX;
extern int g_currentY;
extern int g_hiddenY;

extern UINT_PTR g_hideTimer;
extern bool g_hideTimerActive;

extern const int MOUSE_CHECK_INTERVAL;

extern bool g_isAnimating;
extern bool g_isHiding;
extern int g_animationY;
extern const int ANIMATION_STEP;
extern const int ANIMATION_INTERVAL;
extern const int ANIMATION_TIMER_ID;

extern bool g_isHidden;
extern bool g_waitingForHide;
