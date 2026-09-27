#include "overlay.h"

#include <windowsx.h>

#include <algorithm>
#include <cwchar>
#include <utility>

#include "log.h"
#include "messages.h"
#include "taskbar.h"

namespace tbm {
namespace {

constexpr wchar_t kClassName[] = L"TaskbarMonitorOverlay";
constexpr wchar_t kTextFontFace[] = L"Segoe UI Variable Text";  // The taskbar's own UI font.
constexpr wchar_t kIconFontFace[] = L"Segoe Fluent Icons";      // Windows 11 system icons.
constexpr int kFontSizeDip = 12;
constexpr int kIconGapDip = 4;  // Between an icon and its value.
constexpr int kColumnGapDip = 12;

// One icon per cell, in Cells order (format.h).
constexpr wchar_t kIcons[kRows * kColumns] = {
    0xE950,  // Chip: CPU usage.
    0xEC4A,  // Speedometer: CPU clock.
    0xE9CA,  // Thermometer: temperature.
    0xE945,  // Lightning bolt: CPU package power.
    0xE83F,  // Battery: battery flow, i.e. system power.
    0xE964,  // Memory module: memory load.
};

// Icons are drawn gray instead of white, so the coverage-to-alpha conversion in Render() makes
// them fainter than the values in both themes.
constexpr COLORREF kIconColor = RGB(150, 150, 150);
constexpr COLORREF kValueColor = RGB(255, 255, 255);

// Background pixels: alpha 1 is invisible but keeps the whole rect hit-testable for clicks.
constexpr uint32_t kBackgroundPixel = 0x01000000;

int Scale(int dip, UINT dpi) { return MulDiv(dip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI); }

HFONT CreateUiFont(const wchar_t* face, UINT dpi) {
  return CreateFontW(-Scale(kFontSizeDip, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                     ANTIALIASED_QUALITY,  // Grayscale: ClearType can't do transparency.
                     DEFAULT_PITCH, face);
}

int TextWidth(HDC dc, const wchar_t* text, int length) {
  SIZE extent{};
  GetTextExtentPoint32W(dc, text, length, &extent);
  return extent.cx;
}

}  // namespace

bool Overlay::RegisterWindowClass(HINSTANCE instance) {
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = &Overlay::WindowProc;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.lpszClassName = kClassName;
  return RegisterClassExW(&window_class) != 0;
}

bool Overlay::Create(HINSTANCE instance, const Taskbar& taskbar, HWND owner) {
  Destroy();
  owner_ = owner;
  if (!dc_) dc_.reset(CreateCompatibleDC(nullptr));
  if (!dc_) {
    log::Error(L"overlay: CreateCompatibleDC failed");
    return false;
  }
  // A child of Explorer's taskbar window: Explorer then shows, hides, moves and clips it along
  // with the taskbar (auto-hide, full-screen apps, Start menu).
  CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, kClassName, L"", WS_CHILD | WS_CLIPSIBLINGS, 0,
                  0, 0, 0, taskbar.window(), nullptr, instance, this);
  if (!hwnd_) {
    log::Error(L"overlay: CreateWindowEx failed: %lu", GetLastError());
    return false;
  }
  rect_ = {};
  visible_ = false;
  stale_ = true;
  return true;
}

void Overlay::Destroy() {
  if (!hwnd_) return;
  destroying_ = true;
  DestroyWindow(hwnd_);
  destroying_ = false;
  hwnd_ = nullptr;  // Already cleared by WM_NCDESTROY unless DestroyWindow failed.
  visible_ = false;
}

void Overlay::Show(const Cells& cells, const Taskbar& taskbar) {
  if (!hwnd_) return;
  const UINT dpi = taskbar.dpi();
  if (dpi != dpi_ && !UpdateFonts(dpi)) return Hide();

  RECT rect;
  if (!taskbar.Place(content_width_, &rect)) return Hide();

  const bool moved = !EqualRect(&rect, &rect_);
  if (moved) {
    SetWindowPos(hwnd_, nullptr, rect.left, rect.top, rect.right - rect.left,
                 rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
    rect_ = rect;
  }
  const bool light_theme = taskbar.light_theme();
  if (stale_ || moved || light_theme != light_theme_ || cells != shown_) {
    if (!Render(cells, light_theme)) return Hide();
  }
  if (!visible_) {
    ShowWindow(hwnd_, SW_SHOWNA);
    visible_ = true;
  }
  // Stay above the taskbar's XAML island (a sibling). Touch the z-order only when needed: each
  // SetWindowPos makes Explorer revalidate the taskbar.
  if (GetWindow(taskbar.window(), GW_CHILD) != hwnd_) {
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
}

void Overlay::Hide() {
  if (!visible_) return;
  ShowWindow(hwnd_, SW_HIDE);
  visible_ = false;
}

bool Overlay::UpdateFonts(UINT dpi) {
  UniqueFont text_font(CreateUiFont(kTextFontFace, dpi));
  UniqueFont icon_font(CreateUiFont(kIconFontFace, dpi));
  if (!text_font || !icon_font) {
    log::Error(L"overlay: CreateFont failed");
    return false;
  }

  // Every icon gets the same slot, so values line up whatever their length.
  TEXTMETRICW icon_metrics;
  int icon_width = 0;
  {
    ScopedSelect select(dc_.get(), icon_font.get());
    GetTextMetricsW(dc_.get(), &icon_metrics);
    for (const wchar_t& icon : kIcons)
      icon_width = std::max(icon_width, TextWidth(dc_.get(), &icon, 1));
  }
  value_offset_ = icon_width + Scale(kIconGapDip, dpi);

  ScopedSelect select(dc_.get(), text_font.get());
  TEXTMETRICW text_metrics;
  GetTextMetricsW(dc_.get(), &text_metrics);
  row_height_ = text_metrics.tmHeight;
  icon_top_ = (row_height_ - icon_metrics.tmHeight) / 2;

  const int column_gap = Scale(kColumnGapDip, dpi);
  int x = 0;
  for (int column = 0; column < kColumns; ++column) {
    int widest = 0;
    for (const wchar_t* text : kWidestText[column]) {
      widest = std::max(widest, TextWidth(dc_.get(), text, static_cast<int>(wcslen(text))));
    }
    column_x_[column] = x;
    x += value_offset_ + widest + column_gap;
  }
  content_width_ = x - column_gap;

  text_font_ = std::move(text_font);
  icon_font_ = std::move(icon_font);
  dpi_ = dpi;
  stale_ = true;
  return true;
}

bool Overlay::EnsureSurface(SIZE size) {
  if (bitmap_ && size.cx == surface_.cx && size.cy == surface_.cy) return true;

  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = size.cx;
  info.bmiHeader.biHeight = -size.cy;  // Top-down rows.
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  UniqueBitmap bitmap(CreateDIBSection(dc_.get(), &info, DIB_RGB_COLORS, &bits, nullptr, 0));
  if (!bitmap) {
    log::Error(L"overlay: CreateDIBSection %ldx%ld failed", size.cx, size.cy);
    return false;
  }
  bitmap_ = std::move(bitmap);
  pixels_ = static_cast<uint32_t*>(bits);
  surface_ = size;
  return true;
}

bool Overlay::Render(const Cells& cells, bool light_theme) {
  SIZE size{rect_.right - rect_.left, rect_.bottom - rect_.top};
  if (!EnsureSurface(size)) return false;
  const size_t pixel_count = static_cast<size_t>(size.cx) * static_cast<size_t>(size.cy);
  std::fill_n(pixels_, pixel_count, 0u);

  ScopedSelect select_bitmap(dc_.get(), bitmap_.get());
  SetBkMode(dc_.get(), TRANSPARENT);
  const int top = (size.cy - kRows * row_height_) / 2;
  {
    ScopedSelect select_font(dc_.get(), icon_font_.get());
    SetTextColor(dc_.get(), kIconColor);
    for (int i = 0; i < kRows * kColumns; ++i) {
      TextOutW(dc_.get(), column_x_[i % kColumns], top + (i / kColumns) * row_height_ + icon_top_,
               &kIcons[i], 1);
    }
  }
  {
    ScopedSelect select_font(dc_.get(), text_font_.get());
    SetTextColor(dc_.get(), kValueColor);
    for (int i = 0; i < kRows * kColumns; ++i) {
      const Cell& cell = cells[i];
      TextOutW(dc_.get(), column_x_[i % kColumns] + value_offset_,
               top + (i / kColumns) * row_height_, cell.data(),
               static_cast<int>(wcsnlen(cell.data(), cell.size())));
    }
  }
  GdiFlush();

  // Grayscale text on black: a channel's value is the glyph coverage times the drawing color's
  // brightness. Convert it to the theme's text color with premultiplied alpha.
  for (size_t i = 0; i < pixel_count; ++i) {
    const uint32_t coverage = pixels_[i] & 0xFF;
    if (coverage == 0) {
      pixels_[i] = kBackgroundPixel;
    } else if (light_theme) {
      pixels_[i] = coverage << 24;  // Black.
    } else {
      pixels_[i] = coverage << 24 | coverage << 16 | coverage << 8 | coverage;  // White.
    }
  }

  BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  POINT origin{0, 0};
  if (!UpdateLayeredWindow(hwnd_, nullptr, nullptr, &size, dc_.get(), &origin, 0, &blend,
                           ULW_ALPHA)) {
    log::Error(L"overlay: UpdateLayeredWindow failed: %lu", GetLastError());
    return false;
  }
  shown_ = cells;
  light_theme_ = light_theme;
  stale_ = false;
  return true;
}

LRESULT CALLBACK Overlay::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* self =
        static_cast<Overlay*>(reinterpret_cast<const CREATESTRUCTW*>(lparam)->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  auto* self = reinterpret_cast<Overlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  return self ? self->HandleMessage(message, wparam, lparam)
              : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT Overlay::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
  const HWND hwnd = hwnd_;
  switch (message) {
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_LBUTTONUP:
      PostMessageW(owner_, kMsgOpenTaskManager, 0, 0);
      return 0;
    case WM_RBUTTONUP: {
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ClientToScreen(hwnd, &point);
      PostMessageW(owner_, kMsgShowMenu, 0, MAKELPARAM(point.x, point.y));
      return 0;
    }
    case WM_NCDESTROY:
      // Also reached when Explorer tears the taskbar down; the App then re-attaches.
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      hwnd_ = nullptr;
      visible_ = false;
      if (!destroying_) PostMessageW(owner_, kMsgOverlayLost, 0, 0);
      break;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace tbm
