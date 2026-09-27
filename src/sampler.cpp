#include "sampler.h"

#include <shellapi.h>

#include <iterator>

#include "log.h"
#include "sensors.h"

namespace tbm {
namespace {

// Overridable at build time only for accelerated leak tests (e.g. -DTBM_SAMPLE_PERIOD_MS=20).
#ifndef TBM_SAMPLE_PERIOD_MS
#define TBM_SAMPLE_PERIOD_MS 2000
#endif
constexpr LONG kPeriodMs = TBM_SAMPLE_PERIOD_MS;
// Lets Windows batch this wakeup with others; larger is cheaper, 500 ms is still invisible.
constexpr ULONG kTolerableDelayMs = 500;
// Longest wait for the thread at exit; a sample normally takes a few milliseconds.
constexpr DWORD kStopTimeoutMs = 3000;

bool FullScreenAppInFront() {
  QUERY_USER_NOTIFICATION_STATE state;
  if (FAILED(SHQueryUserNotificationState(&state))) return false;
  return state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN ||
         state == QUNS_PRESENTATION_MODE;
}

}  // namespace

bool Sampler::Start(HWND target, UINT message) {
  target_ = target;
  message_ = message;
  stop_event_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  wake_event_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
  if (!stop_event_ || !wake_event_) {
    log::Error(L"sampler: CreateEvent failed: %lu", GetLastError());
    return false;
  }
  thread_.reset(CreateThread(nullptr, 0, &Sampler::ThreadMain, this, 0, nullptr));
  if (!thread_) {
    log::Error(L"sampler: CreateThread failed: %lu", GetLastError());
    return false;
  }
  return true;
}

void Sampler::Stop() {
  if (!thread_) return;
  SetEvent(stop_event_.get());
  if (WaitForSingleObject(thread_.get(), kStopTimeoutMs) == WAIT_TIMEOUT) {
    // A sensor read is stuck in a driver. Stop is only called on the way out, so let process
    // exit end the thread instead of hanging here; its handles must stay open until then.
    log::Error(L"sampler: thread did not stop within %lu ms", kStopTimeoutMs);
    thread_.release();
    stop_event_.release();
    wake_event_.release();
    return;
  }
  thread_.reset();
  stop_event_.reset();
  wake_event_.reset();
}

void Sampler::SetActive(bool active) {
  if (active_.exchange(active) != active && wake_event_) SetEvent(wake_event_.get());
}

Metrics Sampler::Latest() const {
  AcquireSRWLockShared(&lock_);
  const Metrics copy = latest_;
  ReleaseSRWLockShared(&lock_);
  return copy;
}

DWORD WINAPI Sampler::ThreadMain(void* self) {
  static_cast<Sampler*>(self)->Run();
  return 0;
}

void Sampler::Run() {
  UniqueHandle timer(CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS));
  if (!timer) {
    log::Error(L"sampler: CreateWaitableTimerEx failed: %lu", GetLastError());
    return;
  }
  Sensors sensors;
  sensors.Open();

  bool armed = false;
  bool first_arm = true;
  auto reconcile = [&] {
    const bool want = active_.load();
    if (want && !armed) {
      // The first sample waits a full period so rate counters have a baseline; after a pause
      // it comes at once so stale values don't linger on screen.
      LARGE_INTEGER due;
      due.QuadPart = first_arm ? -static_cast<LONGLONG>(kPeriodMs) * 10'000 : -1;
      SetWaitableTimerEx(timer.get(), &due, kPeriodMs, nullptr, nullptr, nullptr,
                         kTolerableDelayMs);
      armed = true;
      first_arm = false;
    } else if (!want && armed) {
      CancelWaitableTimer(timer.get());
      armed = false;
    }
  };
  reconcile();

  const HANDLE handles[] = {stop_event_.get(), wake_event_.get(), timer.get()};
  for (;;) {
    const DWORD signaled =
        WaitForMultipleObjects(static_cast<DWORD>(std::size(handles)), handles, FALSE, INFINITE);
    if (signaled == WAIT_OBJECT_0) break;
    if (signaled == WAIT_OBJECT_0 + 1) {
      reconcile();
    } else if (signaled == WAIT_OBJECT_0 + 2) {
      if (!FullScreenAppInFront()) Publish(sensors.Read());
    } else {
      log::Error(L"sampler: wait failed: %lu", GetLastError());
      break;
    }
  }
}

void Sampler::Publish(const Metrics& metrics) {
  AcquireSRWLockExclusive(&lock_);
  latest_ = metrics;
  ReleaseSRWLockExclusive(&lock_);
  PostMessageW(target_, message_, 0, 0);
}

}  // namespace tbm
