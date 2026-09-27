#pragma once

#include <windows.h>

#include "format.h"
#include "overlay.h"
#include "sampler.h"
#include "taskbar.h"
#include "win_handle.h"

namespace tbm {

// Owns the components and reacts to system events through a hidden top-level window:
// Explorer restarts, display/setting changes, display power, session lock and disconnect, and
// the overlay's mouse actions.
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
  void ScheduleAttachRetry();
  void CancelAttachRetry();
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
  bool session_connected_ = true;
  UINT retry_delay_ms_ = 0;  // Current re-attach back-off; 0 while attached.
};

}  // namespace tbm
