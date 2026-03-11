#include "app_state.h"

HWND g_hwnd = nullptr;
HWND g_settingsDlg = nullptr;

bool g_use24Hour = true;
bool g_showDate = true;
bool g_showYear = false;
bool g_showSeconds = true;
int g_theme = 0;
bool g_autoHide = false;
bool g_isHovering = false;

int g_windowSize = 1;
int g_position = 1;
int g_hideDelayMinutes = 0;
int g_hideDelaySeconds = 1;

int g_fontSize = 14;
bool g_boldFont = true;

const int SIZE_WIDTH[3] = {120, 144, 168};
const int SIZE_HEIGHT[3] = {20, 25, 30};

int g_currentX = 0;
int g_currentY = 10;
int g_hiddenY = -50;

UINT_PTR g_hideTimer = 0;
bool g_hideTimerActive = false;

const int MOUSE_CHECK_INTERVAL = 100;

bool g_isAnimating = false;
bool g_isHiding = false;
int g_animationY = 10;
const int ANIMATION_STEP = 3;
const int ANIMATION_INTERVAL = 15;
const int ANIMATION_TIMER_ID = 4;

bool g_isHidden = false;
bool g_waitingForHide = false;
