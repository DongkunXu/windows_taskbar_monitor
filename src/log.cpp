#include "log.h"

#include <windows.h>

#include <shlobj.h>

#include <cstdarg>
#include <cwchar>
#include <iterator>

namespace tbm::log {
namespace {

constexpr LONGLONG kMaxFileBytes = 64 * 1024;

SRWLOCK g_lock = SRWLOCK_INIT;
bool g_file_enabled = false;
wchar_t g_dir[MAX_PATH];
wchar_t g_path[MAX_PATH];
wchar_t g_rotated_path[MAX_PATH];

HANDLE OpenForAppend(DWORD disposition) {
  return CreateFileW(g_path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, disposition,
                     FILE_ATTRIBUTE_NORMAL, nullptr);
}

// Opens, appends and closes on every call: writes are rare, and no handle stays open.
void AppendToFile(const char* data, DWORD size) {
  CreateDirectoryW(g_dir, nullptr);  // Fails harmlessly when it already exists.
  HANDLE file = OpenForAppend(OPEN_ALWAYS);
  if (file == INVALID_HANDLE_VALUE) return;

  LARGE_INTEGER current{};
  if (GetFileSizeEx(file, &current) && current.QuadPart + size > kMaxFileBytes) {
    CloseHandle(file);
    MoveFileExW(g_path, g_rotated_path, MOVEFILE_REPLACE_EXISTING);
    file = OpenForAppend(CREATE_ALWAYS);
    if (file == INVALID_HANDLE_VALUE) return;
  }
  DWORD written = 0;
  WriteFile(file, data, size, &written, nullptr);
  CloseHandle(file);
}

void Write(const wchar_t* level, const wchar_t* format, va_list args) {
  wchar_t message[512];
  _vsnwprintf_s(message, std::size(message), _TRUNCATE, format, args);

  SYSTEMTIME now;
  GetLocalTime(&now);
  wchar_t line[640];
  const int length = _snwprintf_s(
      line, std::size(line), _TRUNCATE, L"%04u-%02u-%02u %02u:%02u:%02u %ls %ls\r\n", now.wYear,
      now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, level, message);
  if (length <= 0) return;
  OutputDebugStringW(line);
  if (!g_file_enabled) return;

  char utf8[sizeof(line) * 3 / sizeof(wchar_t)];
  const int bytes =
      WideCharToMultiByte(CP_UTF8, 0, line, length, utf8, sizeof(utf8), nullptr, nullptr);
  if (bytes <= 0) return;

  AcquireSRWLockExclusive(&g_lock);
  AppendToFile(utf8, static_cast<DWORD>(bytes));
  ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace

void Init() {
  // The known-folder API rather than %LOCALAPPDATA%, which the parent process controls.
  wchar_t* base = nullptr;
  const bool found = SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base));
  const bool built =
      found && _snwprintf_s(g_dir, MAX_PATH, _TRUNCATE, L"%ls\\TaskbarMonitor", base) > 0 &&
      _snwprintf_s(g_path, MAX_PATH, _TRUNCATE, L"%ls\\taskbar-monitor.log", g_dir) > 0 &&
      _snwprintf_s(g_rotated_path, MAX_PATH, _TRUNCATE, L"%ls.1", g_path) > 0;
  CoTaskMemFree(base);
  g_file_enabled = built;  // Otherwise debugger output only.
}

void Info(const wchar_t* format, ...) {
  va_list args;
  va_start(args, format);
  Write(L"INFO ", format, args);
  va_end(args);
}

void Error(const wchar_t* format, ...) {
  va_list args;
  va_start(args, format);
  Write(L"ERROR", format, args);
  va_end(args);
}

}  // namespace tbm::log
