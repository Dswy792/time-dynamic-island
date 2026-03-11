#include "actions.h"

#include <windows.h>
#include <shellapi.h>

#include <string>

void OpenTimeSettings()
{
    ShellExecuteW(NULL, L"open", L"control.exe", L"timedate.cpl", NULL, SW_SHOW);
}

void SetAutoStartup()
{
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
                        0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        std::wstring fullPath = L"\"";
        fullPath += path;
        fullPath += L"\"";

        RegSetValueExW(hKey, L"DynamicIsland", 0, REG_SZ,
                       reinterpret_cast<const BYTE *>(fullPath.c_str()),
                       (DWORD)((fullPath.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
}
