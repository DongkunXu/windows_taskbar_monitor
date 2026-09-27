# Taskbar Monitor

**English** | [简体中文](README.zh-CN.md)

Two rows of hardware readings in the empty left part of the Windows 11 taskbar:

```
[chip] 12%     [gauge] 1.8GHz     [thermometer] 54°C
[bolt] 5.2W    [battery] −8.4W    [memory] 61%
```

Each value has a faint icon that tells what it is. Top row: CPU usage, actual clock, temperature. Bottom row: CPU package power, battery power (`−` discharging, which is the whole system's draw; `+` charging; `AC` when on AC with no flow), memory usage.

- No driver, no elevation, no third-party software such as HWiNFO: all data comes from standard Windows interfaces.
- Tiny footprint: a 270 KB executable with two resident threads, sampling every 2 s; sampling stops while the display is off, the session is locked, or a full-screen app is in front.
- Leaves nothing behind: the only file on disk is a log capped at 128 KB.

## How it works

| Reading | Source |
|---|---|
| CPU usage | PDH `% Processor Utility`, the same figure Task Manager shows (can exceed 100%) |
| CPU clock | PDH `Actual Frequency` |
| Temperature | Hottest ACPI thermal zone (PDH `Thermal Zone Information`) |
| CPU power | Intel RAPL package power through the Windows Energy Meter interface |
| Battery power | `CallNtPowerInformation(SystemBatteryState)` |
| Memory | `GlobalMemoryStatusEx` |

The readout is a layered child window of the taskbar, so it hides and moves together with it (auto-hide, full-screen apps, Start menu). Sampling runs on its own thread so the taskbar never waits on it. Unavailable values read `--`.

## Limitations

- The temperature is an ACPI thermal zone, whose meaning is firmware-defined. On most laptops it follows CPU load but is smoother than the CPU package temperature.
- On AC, Windows does not report the adapter's output, so the battery cell shows charging power instead of system power. Battery power is updated by the laptop's firmware and can lag by up to a minute.
- Only the primary taskbar at the bottom of the screen is supported; the readout hides on vertical or top taskbars, with left-aligned icons, or when there isn't enough room.
- It depends on undocumented parts of the Windows 11 taskbar, which a Windows update could change. If that happens, the readout hides rather than crashing.

## Usage

- Run `TaskbarMonitor.exe`. Only one instance runs at a time.
- Left-click: open Task Manager.
- Right-click: toggle "Start with Windows", or exit.

## What it writes

| Location | Content | Cleanup |
|---|---|---|
| `%LOCALAPPDATA%\TaskbarMonitor\taskbar-monitor.log` (and `.log.1`) | Errors and state transitions | Rotates at 64 KB per file, 128 KB at most |
| `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\TaskbarMonitor` | Start with Windows | Removed when "Start with Windows" is turned off in the menu, together with the matching entry of the system's Startup apps switch |

To remove it completely: turn off "Start with Windows" in the menu and exit, then delete the executable and the `%LOCALAPPDATA%\TaskbarMonitor` folder.

## Building

Toolchain: [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (UCRT), CMake 3.25 or later, and Ninja. All of them install per user, without elevation:

```bash
winget install MartinStorsjo.LLVM-MinGW.UCRT Kitware.CMake Ninja-build.Ninja
```

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The executable is `build/release/TaskbarMonitor.exe`; use the `debug` preset for a debug build. All build output goes under `build/`, so deleting that folder cleans everything.

## Layout

```
src/        Source code
res/        Manifest and version resource
tests/      Unit tests
```

## License

[MIT](LICENSE)
