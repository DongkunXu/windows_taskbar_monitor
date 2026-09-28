# Taskbar Monitor

**English** | [简体中文](README.zh-CN.md)

Two rows of hardware readings in the empty left part of the Windows 11 taskbar:

![Taskbar Monitor on the Windows 11 taskbar](assets/screenshot.png)

Each value has a faint icon that tells what it is. Top row: CPU usage, actual clock, temperature. Bottom row: CPU package power, battery power (`−` discharging, which is the whole system's draw; `+` charging; `AC` when on AC with no flow), memory usage.

- No driver, no elevation, no third-party software such as HWiNFO: all data comes from standard Windows interfaces.
- Tiny footprint: a 340 KB executable with two resident threads, sampling every 2 s; sampling stops while the display is off, the session is locked, or a full-screen app is in front.
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

For transparent taskbars (e.g. with TranslucentTB), both ends of the taskbar get a soft shade behind the readout and behind the tray icons and clock: darkest in the bottom corners and fading out diagonally toward the middle and the top edge, so white text stays readable on a bright wallpaper. The shade sits beneath Explorer's own content, so it never dims icons, and a taskbar with an opaque background covers it. It is redrawn only when the taskbar's size or theme changes.

Static content wears OLED panels, so the readout's white is drawn at about 85% brightness, and the readout drifts around its spot by one step (1/96 inch, 2 pixels at 200% scaling) every 3 minutes, so its thin glyph edges don't keep wearing the same pixels. Drifting only moves the window; nothing is redrawn for it.

## Limitations

- The temperature is an ACPI thermal zone, whose meaning is firmware-defined. On most laptops it follows CPU load but is smoother than the CPU package temperature.
- On AC, Windows does not report the adapter's output, so the battery cell shows charging power instead of system power. Battery power is updated by the laptop's firmware and can lag by up to a minute.
- Only the primary taskbar at the bottom of the screen is supported; the readout hides on vertical or top taskbars, with left-aligned icons, or when there isn't enough room.
- It depends on undocumented parts of the Windows 11 taskbar, which a Windows update could change. If that happens, the readout hides rather than crashing.

## Usage

- Run `TaskbarMonitor.exe`. Only one instance runs at a time.
- Left-click: open Task Manager.
- Right-click: toggle "Start with Windows", or exit. "Start with Windows" registers the executable's current path, so turn it on from the installed copy (see [Building](#building)), not from the build folder.

It looks after itself while running: it re-attaches when Explorer restarts, stops sampling while the display is off, the session is locked or disconnected, or a full-screen app is in front, and resumes on its own afterwards.

## What it writes

| Location | Content | Cleanup |
|---|---|---|
| `%LOCALAPPDATA%\TaskbarMonitor\taskbar-monitor.log` (and `.log.1`) | Errors and state transitions | Rotates at 64 KB per file, 128 KB at most |
| `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\TaskbarMonitor` | Start with Windows | Removed when "Start with Windows" is turned off in the menu, together with the matching entry of the system's Startup apps switch |

To remove it completely: turn off "Start with Windows" in the menu and exit, then delete `%LOCALAPPDATA%\Programs\TaskbarMonitor` (the installed executable) and `%LOCALAPPDATA%\TaskbarMonitor` (the log).

## Building

Toolchain: [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (UCRT), CMake 3.25 or later, and Ninja. All of them install per user, without elevation:

```bash
winget install MartinStorsjo.LLVM-MinGW.UCRT Kitware.CMake Ninja-build.Ninja
```

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --install build/release
```

The last step copies the executable to `%LOCALAPPDATA%\Programs\TaskbarMonitor\TaskbarMonitor.exe`; exit a running copy first. Use the `debug` preset for a debug build. All build output goes under `build/`, so deleting that folder cleans everything.

## Layout

```
src/        Source code
res/        Manifest and version resource
tests/      Unit tests
assets/     README images
```

## License

[MIT](LICENSE)
