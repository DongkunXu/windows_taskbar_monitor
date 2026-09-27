#pragma once

// Bounded diagnostic log for errors and state transitions only (never per-sample output).
// File: %LOCALAPPDATA%\TaskbarMonitor\taskbar-monitor.log, rotated to .log.1 at 64 KB, so the
// log never occupies more than 128 KB. Thread-safe.
namespace tbm::log {

void Init();
void Info(const wchar_t* format, ...);
void Error(const wchar_t* format, ...);

}  // namespace tbm::log
