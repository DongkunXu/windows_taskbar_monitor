#pragma once

#include <windows.h>

namespace tbm {

// The primary Windows 11 taskbar: its window, the Start button's position and the settings that
// decide where (and whether) the overlay fits.
//
// Relies on Explorer's legacy windows: "Shell_TrayWnd" and its hidden "Start" child, whose rect
// follows the real, centered Start button. Neither is a documented API.
class Taskbar {
 public:
  // Re-reads alignment, Widgets and theme from the registry. Call on WM_SETTINGCHANGE.
  void ReloadSettings();
  // Finds the taskbar window. False when Explorer has no taskbar right now.
  bool Locate();

  HWND window() const { return tray_; }
  UINT dpi() const;
  bool light_theme() const { return light_theme_; }

  // Rect in taskbar client coordinates for an overlay `width` pixels wide, vertically matching
  // the button row. False when it doesn't fit: vertical or top taskbar, left-aligned icons, or
  // not enough room left of Start.
  bool Place(int width, RECT* rect) const;

 private:
  HWND tray_ = nullptr;
  mutable HWND start_ = nullptr;  // Re-found lazily if Explorer recreated it.
  bool left_aligned_ = false;
  bool widgets_ = true;
  bool light_theme_ = false;
};

}  // namespace tbm
