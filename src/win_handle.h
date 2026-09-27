#pragma once

#include <windows.h>

#include <utility>

namespace tbm {

// Move-only owner of a Win32 handle; `Close` runs when the owner lets go.
template <typename T, auto Close>
class Unique {
 public:
  Unique() = default;
  explicit Unique(T handle) : handle_(handle) {}
  ~Unique() { reset(); }

  Unique(const Unique&) = delete;
  Unique& operator=(const Unique&) = delete;
  Unique(Unique&& other) noexcept : handle_(std::exchange(other.handle_, T{})) {}
  Unique& operator=(Unique&& other) noexcept {
    if (this != &other) reset(std::exchange(other.handle_, T{}));
    return *this;
  }

  T get() const { return handle_; }
  explicit operator bool() const { return handle_ != T{}; }

  void reset(T handle = T{}) {
    if (handle_ != T{}) Close(handle_);
    handle_ = handle;
  }

 private:
  T handle_{};
};

// Handles whose failure value is null (events, threads, timers, mutexes; not CreateFile).
using UniqueHandle = Unique<HANDLE, &::CloseHandle>;
using UniqueDc = Unique<HDC, &::DeleteDC>;
using UniqueFont = Unique<HFONT, &::DeleteObject>;
using UniqueBitmap = Unique<HBITMAP, &::DeleteObject>;

// Selects a GDI object into a DC for the current scope, then restores the previous one, so the
// object is never still selected when its owner deletes it.
class ScopedSelect {
 public:
  ScopedSelect(HDC dc, HGDIOBJ object) : dc_(dc), previous_(SelectObject(dc, object)) {}
  ~ScopedSelect() { SelectObject(dc_, previous_); }

  ScopedSelect(const ScopedSelect&) = delete;
  ScopedSelect& operator=(const ScopedSelect&) = delete;

 private:
  HDC dc_;
  HGDIOBJ previous_;
};

}  // namespace tbm
