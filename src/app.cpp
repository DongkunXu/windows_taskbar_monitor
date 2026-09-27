#include "app.h"

#include <objbase.h>
#include <shellapi.h>
#include <windowsx.h>
#include <wtsapi32.h>

#include <algorithm>

#include "autostart.h"
#include "log.h"
#include "messages.h"

namespace tbm {
namespace {

constexpr wchar_t kClassName[] = L"TaskbarMonitorApp";

// Re-attach attempts back off from 2 s to 1 min, so a missing or crash-looping taskbar never
// keeps the process busy. The timer only exists while detached.
constexpr UINT_PTR kRetryTimerId = 1;
constexpr UINT kFirstRetryMs = 2000;
constexpr UINT kMaxRetryMs = 60000;

enum MenuCommand : UINT { kCommandAutostart = 1, kCommandExit };

// GUID_SESSION_DISPLAY_STATUS: display on/off/dimmed for this session.
constexpr GUID kSessionDisplayStatus = {
    0x2b84c20e, 0xad23, 0x4ddf, {0x93, 0xdb, 0x05, 0xff, 0xbd, 0x7e, 0xfc, 0xa5}};

DWORD WINAPI OpenTaskManagerThread(void*) {
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  ShellExecuteW(nullptr, nullptr, L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
  if (SUCCEEDED(com)) CoUninitialize();
  return 0;
}

// ShellExecute can block, e.g. on a UAC prompt, and the UI thread shares Explorer's input queue,
// so the launch runs on a short-lived thread of its own.
void OpenTaskManager() {
  HANDLE thread = CreateThread(nullptr, 0, &OpenTaskManagerThread, nullptr, 0, nullptr);
  if (thread) {
    CloseHandle(thread);
  } else {
    log::Error(L"starting Task Manager failed: %lu", GetLastError());
  }
}

}  // namespace

int App::Run(HINSTANCE instance) {
  instance_ = instance;

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = &App::WindowProc;
  window_class.hInstance = instance;
  window_class.lpszClassName = kClassName;
  if (!RegisterClassExW(&window_class) || !Overlay::RegisterWindowClass(instance)) {
    log::Error(L"RegisterClassEx failed: %lu", GetLastError());
    return 1;
  }

  // Hidden top-level rather than message-only: only top-level windows receive the TaskbarCreated
  // and WM_SETTINGCHANGE broadcasts.
  if (!CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"Taskbar Monitor", WS_POPUP, 0, 0, 0, 0,
                       nullptr, nullptr, instance, this)) {
    log::Error(L"creating the app window failed: %lu", GetLastError());
    return 1;
  }

  MSG message;
  BOOL result;
  while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return result < 0 ? 1 : static_cast<int>(message.wParam);
}

LRESULT CALLBACK App::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* self = static_cast<App*>(reinterpret_cast<const CREATESTRUCTW*>(lparam)->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  return self ? self->HandleMessage(message, wparam, lparam)
              : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT App::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == taskbar_created_message_ && message != 0) {
    log::Info(L"taskbar recreated");
    overlay_.Destroy();
    Attach();
    return 0;
  }

  switch (message) {
    case WM_CREATE:
      return OnCreate() ? 0 : -1;

    case WM_DESTROY:
      OnDestroy();
      PostQuitMessage(0);
      return 0;

    case kMsgSample:
      OnSample();
      return 0;

    case kMsgOverlayLost:
      // Explorer usually announces its new taskbar with TaskbarCreated; the retry covers the rest.
      log::Info(L"overlay lost");
      ScheduleAttachRetry();
      return 0;

    case WM_TIMER:
      if (wparam == kRetryTimerId) {
        KillTimer(hwnd_, kRetryTimerId);  // One-shot; Attach() reschedules on failure.
        Attach();
      }
      return 0;

    case WM_SETTINGCHANGE:
      taskbar_.ReloadSettings();
      overlay_.Show(cells_, taskbar_);
      return 0;

    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
      overlay_.Show(cells_, taskbar_);
      return 0;

    case WM_POWERBROADCAST:
      if (wparam == PBT_POWERSETTINGCHANGE) {
        const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lparam);
        if (IsEqualGUID(setting->PowerSetting, kSessionDisplayStatus) &&
            setting->DataLength >= sizeof(DWORD)) {
          display_on_ = *reinterpret_cast<const DWORD*>(setting->Data) != 0;  // 0 = off.
          UpdateSampling();
        }
      }
      return TRUE;

    case WM_WTSSESSION_CHANGE:
      switch (wparam) {
        case WTS_SESSION_LOCK:
        case WTS_SESSION_UNLOCK:
          session_locked_ = wparam == WTS_SESSION_LOCK;
          break;
        case WTS_CONSOLE_DISCONNECT:
        case WTS_REMOTE_DISCONNECT:
        case WTS_CONSOLE_CONNECT:
        case WTS_REMOTE_CONNECT:
          // Fast user switching and Remote Desktop: nobody sees a disconnected session.
          session_connected_ = wparam == WTS_CONSOLE_CONNECT || wparam == WTS_REMOTE_CONNECT;
          break;
        default:
          return 0;
      }
      UpdateSampling();
      return 0;

    case kMsgOpenTaskManager:
      OpenTaskManager();
      return 0;

    case kMsgShowMenu:
      ShowMenu({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
      return 0;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

bool App::OnCreate() {
  taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
  taskbar_.ReloadSettings();
  // Both deliver the current state right away and on every change.
  display_notification_.reset(
      RegisterPowerSettingNotification(hwnd_, &kSessionDisplayStatus, DEVICE_NOTIFY_WINDOW_HANDLE));
  session_notification_ = WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION);
  if (!display_notification_ || !session_notification_) {
    log::Error(L"power/session notification registration failed: %lu", GetLastError());
  }
  if (!sampler_.Start(hwnd_, kMsgSample)) return false;
  Attach();
  return true;
}

void App::OnDestroy() {
  sampler_.Stop();
  overlay_.Destroy();
  CancelAttachRetry();
  display_notification_.reset();
  if (session_notification_) WTSUnRegisterSessionNotification(hwnd_);
}

void App::OnSample() {
  cells_ = FormatMetrics(sampler_.Latest());
  if (overlay_.alive() && !IsWindow(taskbar_.window())) {
    // Explorer went away without our window being destroyed first.
    overlay_.Destroy();
    ScheduleAttachRetry();
  }
  overlay_.Show(cells_, taskbar_);
}

void App::Attach() {
  if (taskbar_.Locate() && overlay_.Create(instance_, taskbar_, hwnd_)) {
    CancelAttachRetry();
    log::Info(L"attached to the taskbar");
    overlay_.Show(cells_, taskbar_);
    return;
  }
  ScheduleAttachRetry();
}

void App::ScheduleAttachRetry() {
  if (retry_delay_ms_ == 0) {
    log::Info(L"taskbar unavailable, retrying every %u to %u s", kFirstRetryMs / 1000,
              kMaxRetryMs / 1000);
    retry_delay_ms_ = kFirstRetryMs;
  } else {
    retry_delay_ms_ = std::min(retry_delay_ms_ * 2, kMaxRetryMs);
  }
  SetCoalescableTimer(hwnd_, kRetryTimerId, retry_delay_ms_, nullptr, retry_delay_ms_ / 4);
}

void App::CancelAttachRetry() {
  if (retry_delay_ms_ == 0) return;
  KillTimer(hwnd_, kRetryTimerId);
  retry_delay_ms_ = 0;
}

void App::UpdateSampling() {
  sampler_.SetActive(display_on_ && !session_locked_ && session_connected_);
}

void App::ShowMenu(POINT point) {
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  const bool autostart_enabled = autostart::IsEnabled();
  AppendMenuW(menu, MF_STRING | (autostart_enabled ? MF_CHECKED : MF_UNCHECKED), kCommandAutostart,
              L"Start with Windows");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kCommandExit, L"Exit");

  // Foreground is required for the menu to close when the user clicks elsewhere.
  SetForegroundWindow(hwnd_);
  const UINT command =
      TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                       point.x, point.y, hwnd_, nullptr);
  PostMessageW(hwnd_, WM_NULL, 0, 0);
  DestroyMenu(menu);

  switch (command) {
    case kCommandAutostart:
      if (!autostart::SetEnabled(!autostart_enabled)) {
        log::Error(L"changing autostart failed");
      }
      break;
    case kCommandExit:
      DestroyWindow(hwnd_);
      break;
  }
}

}  // namespace tbm
