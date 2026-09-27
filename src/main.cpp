#include <windows.h>

#include "app.h"
#include "log.h"
#include "version.h"
#include "win_handle.h"

#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#endif

namespace {

constexpr wchar_t kInstanceMutex[] = L"Local\\TaskbarMonitor-5d1c7a52-8f0e-4a8b-9d3c-2b6e4f7a1c90";

// EcoQoS: the work here is tiny and latency-insensitive, so Windows may run it on efficient cores
// at low clocks. Also opt out of timer-resolution requests made on our behalf.
void EnableEfficiencyMode() {
  PROCESS_POWER_THROTTLING_STATE state{};
  state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  state.ControlMask =
      PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
  state.StateMask = state.ControlMask;
  SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  tbm::UniqueHandle instance_mutex(CreateMutexW(nullptr, FALSE, kInstanceMutex));
  if (!instance_mutex || GetLastError() == ERROR_ALREADY_EXISTS) return 0;

  EnableEfficiencyMode();
  tbm::log::Init();
  tbm::log::Info(L"started, version %ls", L"" TBM_VERSION_STRING);

  tbm::App app;
  const int exit_code = app.Run(instance);
  tbm::log::Info(L"exited with code %d", exit_code);
  return exit_code;
}
