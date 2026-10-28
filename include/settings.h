#pragma once

#include <windows.h>

#include "app_state.h"

// Load persisted settings into g_settings (performs one-time migration of the
// legacy HideDelayMinutes/HideDelaySeconds values and clamps everything).
void LoadSettings();

// Persist g_settings (including the auto-start registry entry). Returns true
// when the values were written successfully.
bool SaveSettings();

// Clamp / normalize every field of the given settings object.
void ValidateSettings(AppSettings& settings);

// Actual state of the "run at login" registry entry.
bool IsAutoStartEnabled();
void SetAutoStartEnabled(bool enabled);
