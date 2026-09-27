#pragma once

#include <windows.h>

#include <atomic>

#include "metrics.h"
#include "win_handle.h"

namespace tbm {

// Samples on a worker thread and posts `message` to `target` after each sample. The UI thread
// shares Explorer's input queue, so it must never wait on a slow sensor read.
//
// Timing uses one coalescable waitable timer. While inactive (display off, session locked) the
// timer is cancelled and the thread sleeps without any wakeup. Samples are skipped while a
// full-screen application is in front.
class Sampler {
 public:
  Sampler() = default;
  ~Sampler() { Stop(); }
  Sampler(const Sampler&) = delete;
  Sampler& operator=(const Sampler&) = delete;

  bool Start(HWND target, UINT message);
  void Stop();
  void SetActive(bool active);
  Metrics Latest() const;

 private:
  static DWORD WINAPI ThreadMain(void* self);
  void Run();
  void Publish(const Metrics& metrics);

  HWND target_ = nullptr;
  UINT message_ = 0;
  UniqueHandle thread_;
  UniqueHandle stop_event_;
  UniqueHandle wake_event_;  // Signals a change of `active_`.
  std::atomic<bool> active_{true};

  mutable SRWLOCK lock_ = SRWLOCK_INIT;
  Metrics latest_;
};

}  // namespace tbm
