#include "taskbar.h"

namespace tbm {
namespace {

constexpr wchar_t kAdvancedKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
constexpr wchar_t kPersonalizeKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr int kEdgeMarginDip = 12;  // Gap to the screen edge and to the Start button.

DWORD ReadUserDword(const wchar_t* key, const wchar_t* name, DWORD fallback) {
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, &value, &size) !=
      ERROR_SUCCESS) {
    return fallback;
  }
  return value;
}

}  // namespace

void Taskbar::ReloadSettings() {
  // Absent values mean the Windows 11 defaults: centered icons, Widgets on, dark taskbar.
  left_aligned_ = ReadUserDword(kAdvancedKey, L"TaskbarAl", 1) == 0;
  widgets_ = ReadUserDword(kAdvancedKey, L"TaskbarDa", 1) != 0;
  light_theme_ = ReadUserDword(kPersonalizeKey, L"SystemUsesLightTheme", 0) != 0;
}

bool Taskbar::Locate() {
  tray_ = FindWindowW(L"Shell_TrayWnd", nullptr);
  start_ = nullptr;
  return tray_ != nullptr;
}

UINT Taskbar::dpi() const {
  const UINT dpi = tray_ ? GetDpiForWindow(tray_) : 0;
  return dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
}

bool Taskbar::Place(int width, RECT* rect) const {
  RECT tray;
  if (!tray_ || left_aligned_ || !GetWindowRect(tray_, &tray)) return false;
  if (tray.right - tray.left <= tray.bottom - tray.top) return false;  // Vertical taskbar.

  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  if (!GetMonitorInfoW(MonitorFromWindow(tray_, MONITOR_DEFAULTTONEAREST), &monitor)) return false;
  if (tray.top + tray.bottom < monitor.rcMonitor.top + monitor.rcMonitor.bottom) {
    return false;  // Top taskbar.
  }

  if (!start_ || !IsWindow(start_)) start_ = FindWindowExW(tray_, nullptr, L"Start", nullptr);
  RECT start;
  if (!start_ || !GetWindowRect(start_, &start)) return false;

  const int margin = MulDiv(kEdgeMarginDip, static_cast<int>(dpi()), USER_DEFAULT_SCREEN_DPI);
  const int start_left = start.left - tray.left;
  // Widgets occupy the far left, so hug the Start button instead of the screen edge.
  const int left = widgets_ ? start_left - margin - width : margin;
  if (left < margin || left + width > start_left - margin) return false;

  const int top = start.top - tray.top;
  *rect = {left, top, left + width, top + (start.bottom - start.top)};
  return true;
}

}  // namespace tbm
