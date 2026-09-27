#pragma once

#include <windows.h>

#include <cstdint>

#include "format.h"
#include "win_handle.h"

namespace tbm {

class Taskbar;

// The readout: a layered child window of the taskbar, drawn with per-pixel alpha through
// UpdateLayeredWindow. Explorer moves, hides and clips it together with the taskbar. Each cell is a
// faint icon followed by its value.
//
// GDI resources are fixed in number (two fonts, one memory DC, one DIB) and rebuilt only when the
// DPI or window size changes.
class Overlay {
 public:
  static bool RegisterWindowClass(HINSTANCE instance);

  Overlay() = default;
  ~Overlay() { Destroy(); }
  Overlay(const Overlay&) = delete;
  Overlay& operator=(const Overlay&) = delete;

  // `owner` receives the kMsg* notifications from messages.h.
  bool Create(HINSTANCE instance, const Taskbar& taskbar, HWND owner);
  void Destroy();
  bool alive() const { return hwnd_ != nullptr; }

  // Positions and redraws as needed. Does nothing when neither the text, the placement, the DPI
  // nor the theme changed since the last call.
  void Show(const Cells& cells, const Taskbar& taskbar);

 private:
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

  bool UpdateFonts(UINT dpi);
  bool EnsureSurface(SIZE size);
  bool Render(const Cells& cells, bool light_theme);
  void Hide();

  HWND hwnd_ = nullptr;
  HWND owner_ = nullptr;
  bool destroying_ = false;

  UniqueDc dc_;
  UniqueFont text_font_;
  UniqueFont icon_font_;
  UniqueBitmap bitmap_;
  uint32_t* pixels_ = nullptr;  // Premultiplied BGRA, owned by bitmap_.
  SIZE surface_{};

  // Layout, in physical pixels for dpi_.
  UINT dpi_ = 0;
  int column_x_[kColumns]{};  // Left edge of each column, where its icons go.
  int value_offset_ = 0;      // From a column's left edge to its values.
  int icon_top_ = 0;          // Centers the icon within a text row.
  int content_width_ = 0;
  int row_height_ = 0;

  // What is currently on screen.
  Cells shown_{};
  RECT rect_{};
  bool light_theme_ = false;
  bool visible_ = false;
  bool stale_ = true;  // Forces the next Show() to redraw.
};

}  // namespace tbm
