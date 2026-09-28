#include "shade.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "log.h"
#include "taskbar.h"
#include "win_handle.h"

namespace tbm {
namespace {

constexpr wchar_t kClassName[] = L"TaskbarMonitorShade";

// The shade is an elliptical gradient centered on the bottom corner at its end of the taskbar,
// reaching kReach times the content's width across and exactly the taskbar's height up, so it
// has faded to nothing at the top edge, where the taskbar meets the desktop.
constexpr float kStrength = 0.8f;  // Opacity in the corner.
constexpr float kReach = 1.8f;

// Opacity at `r`, the distance from the corner relative to the ellipse. It still darkens right up
// to the corner: a curve that flattens there reads as a dark band with a lighter strip below it.
// It ends flat at r = 1, so the outline never shows.
float Falloff(float r) { return r < 1 ? (1 - r) * (1 - r) * (1 + 1.5f * r) : 0; }

// True when `window` comes after `above` in their parent's z-order, i.e. is drawn below it.
bool IsBelow(HWND window, HWND above) {
  for (HWND next = GetWindow(above, GW_HWNDNEXT); next; next = GetWindow(next, GW_HWNDNEXT)) {
    if (next == window) return true;
  }
  return false;
}

}  // namespace

bool Shade::RegisterWindowClass(HINSTANCE instance) {
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = &Shade::WindowProc;
  window_class.hInstance = instance;
  window_class.lpszClassName = kClassName;
  return RegisterClassExW(&window_class) != 0;
}

bool Shade::Create(HINSTANCE instance, const Taskbar& taskbar) {
  Destroy();
  // WS_EX_TRANSPARENT with WS_EX_LAYERED: clicks pass through to whatever is below. No
  // WS_CLIPSIBLINGS: the island above covers the whole taskbar and would clip everything away.
  CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE, kClassName, L"", WS_CHILD,
                  0, 0, 0, 0, taskbar.window(), nullptr, instance, this);
  if (!hwnd_) {
    if (!failure_reported_) log::Error(L"shade: CreateWindowEx failed: %lu", GetLastError());
    failure_reported_ = true;
    return false;
  }
  rect_ = {};
  visible_ = false;
  stale_ = true;
  return true;
}

void Shade::Destroy() {
  if (!hwnd_) return;
  DestroyWindow(hwnd_);
  hwnd_ = nullptr;  // Already cleared by WM_NCDESTROY unless DestroyWindow failed.
  visible_ = false;
}

void Shade::Show(const Taskbar& taskbar, int content) {
  if (!hwnd_) return;
  SIZE bar;
  const HWND island = taskbar.island();
  if (content <= 0 || !island || !taskbar.Size(&bar)) return Hide();

  const int width = std::min<int>(bar.cx, static_cast<int>(std::ceil(content * kReach)));
  const RECT rect =
      edge_ == Edge::kLeft ? RECT{0, 0, width, bar.cy} : RECT{bar.cx - width, 0, bar.cx, bar.cy};
  const bool moved = !EqualRect(&rect, &rect_);
  if (moved) {
    SetWindowPos(hwnd_, nullptr, rect.left, rect.top, width, bar.cy, SWP_NOZORDER | SWP_NOACTIVATE);
    rect_ = rect;
  }
  const bool light_theme = taskbar.light_theme();
  if (stale_ || moved || content != content_ || light_theme != light_theme_) {
    if (!Render({width, bar.cy}, content, light_theme)) return Hide();
    content_ = content;
    light_theme_ = light_theme;
    stale_ = false;
  }
  if (!visible_) {
    ShowWindow(hwnd_, SW_SHOWNA);
    visible_ = true;
  }
  // Right below the XAML island, which draws Explorer's content. As with the overlay, the
  // z-order is touched only when Explorer has changed it.
  if (!IsBelow(hwnd_, island)) {
    SetWindowPos(hwnd_, island, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
}

void Shade::Hide() {
  if (!visible_) return;
  ShowWindow(hwnd_, SW_HIDE);
  visible_ = false;
}

bool Shade::Render(SIZE size, int content, bool light_theme) {
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = size.cx;
  info.bmiHeader.biHeight = -size.cy;  // Top-down rows.
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  UniqueDc dc(CreateCompatibleDC(nullptr));
  UniqueBitmap bitmap(dc ? CreateDIBSection(dc.get(), &info, DIB_RGB_COLORS, &bits, nullptr, 0)
                         : nullptr);
  if (!bitmap) {
    if (!failure_reported_) log::Error(L"shade: creating the bitmap failed: %lu", GetLastError());
    failure_reported_ = true;
    return false;
  }

  // Premultiplied black, or white on a light taskbar, whose text is black.
  auto* pixels = static_cast<uint32_t*>(bits);
  const float reach = content * kReach;
  for (int y = 0; y < size.cy; ++y) {
    const float up = (size.cy - y - 0.5f) / static_cast<float>(size.cy);
    for (int x = 0; x < size.cx; ++x) {
      const float across = (edge_ == Edge::kLeft ? x + 0.5f : size.cx - x - 0.5f) / reach;
      const float opacity = kStrength * Falloff(std::sqrt(across * across + up * up));
      const auto alpha = static_cast<uint32_t>(std::lround(255 * opacity));
      *pixels++ = light_theme ? alpha * 0x01010101u : alpha << 24;
    }
  }

  ScopedSelect select_bitmap(dc.get(), bitmap.get());
  BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  POINT origin{0, 0};
  if (!UpdateLayeredWindow(hwnd_, nullptr, nullptr, &size, dc.get(), &origin, 0, &blend,
                           ULW_ALPHA)) {
    if (!failure_reported_) log::Error(L"shade: UpdateLayeredWindow failed: %lu", GetLastError());
    failure_reported_ = true;
    return false;
  }
  if (failure_reported_) {
    log::Info(L"shade: drawing recovered");
    failure_reported_ = false;
  }
  return true;
}

LRESULT CALLBACK Shade::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* self =
        static_cast<Shade*>(reinterpret_cast<const CREATESTRUCTW*>(lparam)->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else if (message == WM_NCDESTROY) {
    // Also reached when Explorer tears the taskbar down; the App re-creates the shades together
    // with the overlay.
    if (auto* self = reinterpret_cast<Shade*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      self->hwnd_ = nullptr;
      self->visible_ = false;
    }
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace tbm
