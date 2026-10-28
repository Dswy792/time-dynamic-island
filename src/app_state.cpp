#include "app_state.h"

// Global application state. Kept intentionally small: everything that both the
// UI and the logic modules need lives here.
AppSettings g_settings;
HWND g_hwnd = nullptr;
