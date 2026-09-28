#include "taskbar.h"

#include <algorithm>

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

HWND FindChild(HWND parent, HWND* cached, const wchar_t* class_name) {
  if (!*cached || !IsWindow(*cached)) *cached = FindWindowExW(parent, nullptr, class_name, nullptr);
  return *cached;
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
  start_ = notify_ = island_ = nullptr;
  return tray_ != nullptr;
}

HWND Taskbar::island() const {
  return tray_ ? FindChild(tray_, &island_, L"Windows.UI.Composition.DesktopWindowContentBridge")
               : nullptr;
}

UINT Taskbar::dpi() const {
  const UINT dpi = tray_ ? GetDpiForWindow(tray_) : 0;
  return dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
}

bool Taskbar::BottomRect(RECT* tray) const {
  if (!tray_ || !GetWindowRect(tray_, tray)) return false;
  if (tray->right - tray->left <= tray->bottom - tray->top) return false;  // Vertical taskbar.

  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  if (!GetMonitorInfoW(MonitorFromWindow(tray_, MONITOR_DEFAULTTONEAREST), &monitor)) return false;
  return tray->top + tray->bottom >= monitor.rcMonitor.top + monitor.rcMonitor.bottom;  // Not top.
}

bool Taskbar::Size(SIZE* size) const {
  RECT tray;
  if (!BottomRect(&tray)) return false;
  *size = {tray.right - tray.left, tray.bottom - tray.top};
  return true;
}

int Taskbar::NotifyAreaWidth() const {
  RECT tray, notify;
  if (!tray_ || !GetWindowRect(tray_, &tray)) return 0;
  const HWND window = FindChild(tray_, &notify_, L"TrayNotifyWnd");
  if (!window || !GetWindowRect(window, &notify)) return 0;
  return std::max(0L, tray.right - notify.left);
}

bool Taskbar::Place(int width, RECT* rect) const {
  RECT tray;
  if (left_aligned_ || !BottomRect(&tray)) return false;

  RECT start;
  const HWND start_window = FindChild(tray_, &start_, L"Start");
  if (!start_window || !GetWindowRect(start_window, &start)) return false;

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
