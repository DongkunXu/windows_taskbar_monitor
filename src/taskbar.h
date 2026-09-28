#pragma once

#include <windows.h>

namespace tbm {

// The primary Windows 11 taskbar: its window, the Start button's position and the settings that
// decide where (and whether) the overlay fits.
//
// Relies on Explorer's window structure, none of it a documented API: "Shell_TrayWnd"; its hidden
// "Start" and "TrayNotifyWnd" children, whose rects follow the real Start button and notification
// area; and the XAML island child that draws the taskbar's content.
class Taskbar {
 public:
  // Re-reads alignment, Widgets and theme from the registry. Call on WM_SETTINGCHANGE.
  void ReloadSettings();
  // Finds the taskbar window. False when Explorer has no taskbar right now.
  bool Locate();

  HWND window() const { return tray_; }
  HWND island() const;  // Null when not found.
  UINT dpi() const;
  bool light_theme() const { return light_theme_; }

  // Client size of a horizontal taskbar at the bottom of its monitor. False for any other.
  bool Size(SIZE* size) const;
  // Width of the notification area (tray icons and clock) at the right end, or 0 when unknown.
  int NotifyAreaWidth() const;

  // Rect in taskbar client coordinates for an overlay `width` pixels wide, vertically matching
  // the button row. False when it doesn't fit: vertical or top taskbar, left-aligned icons, or
  // not enough room left of Start.
  bool Place(int width, RECT* rect) const;

 private:
  bool BottomRect(RECT* tray) const;

  HWND tray_ = nullptr;
  // Children, found lazily and again whenever Explorer has recreated them.
  mutable HWND start_ = nullptr;
  mutable HWND notify_ = nullptr;
  mutable HWND island_ = nullptr;
  bool left_aligned_ = false;
  bool widgets_ = true;
  bool light_theme_ = false;
};

}  // namespace tbm
