#include "autostart.h"

#include <windows.h>

#include <cwchar>
#include <iterator>

namespace tbm::autostart {
namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kApprovedKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
constexpr wchar_t kValueName[] = L"TaskbarMonitor";

using CommandLine = wchar_t[MAX_PATH + 2];

bool ThisCommandLine(CommandLine& command) {
  wchar_t exe[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return false;
  return _snwprintf_s(command, std::size(command), _TRUNCATE, L"\"%ls\"", exe) > 0;
}

// Settings > Apps > Startup stores its switch here; an odd first byte means "turned off".
bool DisabledInSettings() {
  BYTE data[12] = {};
  DWORD size = sizeof(data);
  return RegGetValueW(HKEY_CURRENT_USER, kApprovedKey, kValueName, RRF_RT_REG_BINARY, nullptr, data,
                      &size) == ERROR_SUCCESS &&
         size > 0 && (data[0] & 1) != 0;
}

LSTATUS DeleteValue(const wchar_t* key_path) {
  const LSTATUS status = RegDeleteKeyValueW(HKEY_CURRENT_USER, key_path, kValueName);
  return status == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : status;
}

}  // namespace

bool IsEnabled() {
  CommandLine expected;
  CommandLine actual;
  DWORD size = sizeof(actual);
  if (!ThisCommandLine(expected) ||
      RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ, nullptr, actual, &size) !=
          ERROR_SUCCESS) {
    return false;
  }
  return CompareStringOrdinal(expected, -1, actual, -1, TRUE) == CSTR_EQUAL &&
         !DisabledInSettings();
}

bool SetEnabled(bool enabled) {
  // A stale "turned off" switch would silently override the Run entry.
  if (DeleteValue(kApprovedKey) != ERROR_SUCCESS) return false;
  if (!enabled) return DeleteValue(kRunKey) == ERROR_SUCCESS;

  CommandLine command;
  if (!ThisCommandLine(command)) return false;
  const DWORD bytes = static_cast<DWORD>((wcslen(command) + 1) * sizeof(wchar_t));
  return RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName, REG_SZ, command, bytes) ==
         ERROR_SUCCESS;
}

}  // namespace tbm::autostart
