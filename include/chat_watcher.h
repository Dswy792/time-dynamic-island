#pragma once

#include <windows.h>

// UI Automation based watcher for messaging apps that do not raise Windows
// toast notifications at all (WeChat 4.x does not register with the Windows
// notification centre). It reads the WeChat conversation list through the
// documented Windows UI Automation API - it never injects into, hooks or reads
// the memory of the WeChat process.
//
// New incoming messages are posted to the target window as kMsgNotification
// (LPARAM = heap AppNotification*, same contract as the toast listener).
void InitChatWatcher(HWND targetHwnd);
void ShutdownChatWatcher();
