#pragma once

#include <windows.h>
#include <string>

#include "app_state.h"

// Formatting helpers. The "For" variants use the supplied settings so the
// settings UI can render a live preview of unsaved changes.
std::wstring FormatTimeText(const AppSettings& settings);
std::wstring FormatDateText(const AppSettings& settings);
std::wstring FormatDisplayText(const AppSettings& settings);
std::wstring FormatTooltipText(const AppSettings& settings);

// Convenience wrappers using the global g_settings.
std::wstring GetTimeText();
std::wstring GetDateText();
std::wstring GetDisplayText();
std::wstring GetTooltipText();
