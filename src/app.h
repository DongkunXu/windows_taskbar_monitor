#pragma once

#include <windows.h>

#include "format.h"
#include "overlay.h"
#include "sampler.h"
#include "taskbar.h"
#include "win_handle.h"

namespace tbm {

// Owns the components and reacts to system events through a hidden top-level window:
// Explorer restarts, display/setting changes, display power, session lock, and the overlay's
// mouse actions.
class App {
 public:
  int Run(HINSTANCE instance);

 private:
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

  bool OnCreate();
  void OnDestroy();
  void OnSample();
  void Attach();
  void UpdateSampling();
  void ShowMenu(POINT point);

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  UINT taskbar_created_message_ = 0;

  Taskbar taskbar_;
  Overlay overlay_;
  Sampler sampler_;
  Cells cells_ = FormatMetrics({});

  Unique<HPOWERNOTIFY, &::UnregisterPowerSettingNotification> display_notification_;
  bool session_notification_ = false;
  bool display_on_ = true;
  bool session_locked_ = false;
  bool retrying_attach_ = false;
};

}  // namespace tbm
