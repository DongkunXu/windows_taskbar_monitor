#pragma once

#include <windows.h>

namespace tbm {

class Taskbar;

// A soft shade at one end of the taskbar that keeps text readable on a transparent taskbar
// (e.g. TranslucentTB): darkest in that end's bottom corner and fading out diagonally, toward
// the middle and the top edge, across about the width of the content there.
//
// It is a click-through layered child of the taskbar placed below the taskbar's XAML island, so
// Explorer's own icons and text stay on top of it and an opaque taskbar background hides it.
// The pixels are rendered only when the size or theme changes; DWM keeps its own copy, so the
// bitmap is freed right after each update.
class Shade {
 public:
  enum class Edge { kLeft, kRight };

  static bool RegisterWindowClass(HINSTANCE instance);

  explicit Shade(Edge edge) : edge_(edge) {}
  ~Shade() { Destroy(); }
  Shade(const Shade&) = delete;
  Shade& operator=(const Shade&) = delete;

  bool Create(HINSTANCE instance, const Taskbar& taskbar);
  void Destroy();

  // Reaches about `content` pixels in from the edge; 0 hides.
  void Show(const Taskbar& taskbar, int content);

 private:
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

  bool Render(SIZE size, int content, bool light_theme);
  void Hide();

  const Edge edge_;
  HWND hwnd_ = nullptr;

  // What is currently on screen.
  RECT rect_{};
  int content_ = 0;
  bool light_theme_ = false;
  bool visible_ = false;
  bool stale_ = true;
  bool failure_reported_ = false;
};

}  // namespace tbm
