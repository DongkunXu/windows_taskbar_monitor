# Taskbar Monitor

[English](README.md) | **简体中文**

在 Windows 11 任务栏左侧空白处显示两行硬件读数：

![Windows 11 任务栏上的 Taskbar Monitor](assets/screenshot.png)

每个数值前有一个灰色小图标标明含义。第一行依次是 CPU 占用、实际频率、温度；第二行依次是 CPU 封装功耗、电池功率（`−` 放电，即整机功耗；`+` 充电；`AC` 表示接电且不充不放）、内存占用。

- 不装驱动，不需要管理员权限，不依赖 HWiNFO 等第三方软件：数据全部来自 Windows 自带接口。
- 开销很小：一个 340 KB 的可执行文件，常驻 2 个线程，每 2 秒采样一次；熄屏、锁屏或全屏应用在前台时停止采样。
- 不留垃圾：磁盘上只有一份上限 128 KB 的日志。

## 工作原理

| 读数 | 来源 |
|---|---|
| CPU 占用 | PDH `% Processor Utility`，与任务管理器显示的一致（可超过 100%） |
| CPU 频率 | PDH `Actual Frequency` |
| 温度 | 最热的 ACPI 热区（PDH `Thermal Zone Information`） |
| CPU 功耗 | 通过 Windows Energy Meter 接口读取的 Intel RAPL 封装功耗 |
| 电池功率 | `CallNtPowerInformation(SystemBatteryState)` |
| 内存 | `GlobalMemoryStatusEx` |

读数窗口是任务栏的分层子窗口，会随任务栏一起隐藏和移动（自动隐藏、全屏应用、开始菜单）。采样在独立线程进行，任务栏不会因此等待。拿不到的值显示 `--`。

针对透明任务栏（例如 TranslucentTB），任务栏两端会在读数和托盘图标、时钟后面各加一层柔和的暗色渐变，向中间逐渐淡出，让白字在亮色壁纸上也看得清。暗层位于 Explorer 自身内容的下方，不会压暗图标；任务栏背景不透明时会被背景盖住。它只在任务栏尺寸或主题变化时重绘。

## 限制

- 温度来自 ACPI 热区，含义由厂商固件定义。在大多数笔记本上它跟随 CPU 负载，但比 CPU 封装温度更平滑。
- 插电时 Windows 不提供适配器的输出功率，所以电池一格显示的是充电功率，而不是整机功耗。电池功率由笔记本固件更新，可能滞后最多约一分钟。
- 只支持位于屏幕底部的主任务栏；任务栏竖向或在顶部、图标左对齐、空间不够时会隐藏。
- 依赖 Windows 11 任务栏的非公开结构，Windows 更新可能使其改变。届时读数会隐藏，而不会崩溃。

## 使用

- 运行 `TaskbarMonitor.exe`，同一时间只会有一个实例。
- 左键点击：打开任务管理器。
- 右键点击：切换"Start with Windows"（开机自启），或退出（Exit）。开机自启登记的是可执行文件当前所在的路径，所以请从安装后的副本开启（见[构建](#构建)），不要从构建目录开启。

运行期间它会自己照看自己：Explorer 重启后自动重新挂到任务栏；熄屏、锁屏、会话断开或全屏应用在前台时停止采样，之后自动恢复。

## 程序会写入的内容

| 位置 | 内容 | 何时清理 |
|---|---|---|
| `%LOCALAPPDATA%\TaskbarMonitor\taskbar-monitor.log`（及 `.log.1`） | 错误和状态切换 | 单文件到 64 KB 自动轮转，总量不超过 128 KB |
| `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\TaskbarMonitor` | 开机自启 | 在右键菜单里关闭开机自启时删除，同时删除系统"启动应用"开关的对应项 |

彻底移除：先在右键菜单里关闭开机自启并退出，再删除 `%LOCALAPPDATA%\Programs\TaskbarMonitor`（安装的可执行文件）和 `%LOCALAPPDATA%\TaskbarMonitor`（日志）两个目录。

## 构建

工具链：[llvm-mingw](https://github.com/mstorsjo/llvm-mingw)（UCRT 版）、CMake 3.25 及以上、Ninja。都可以按用户安装，不需要管理员权限：

```bash
winget install MartinStorsjo.LLVM-MinGW.UCRT Kitware.CMake Ninja-build.Ninja
```

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --install build/release
```

最后一步把可执行文件复制到 `%LOCALAPPDATA%\Programs\TaskbarMonitor\TaskbarMonitor.exe`，执行前请先退出正在运行的副本。调试版用 `debug` preset。所有构建输出都在 `build/` 下，删掉这个目录即可清理干净。

## 目录结构

```
src/        源代码
res/        manifest 与版本资源
tests/      单元测试
assets/     README 图片
```

## 许可证

[MIT](LICENSE)
