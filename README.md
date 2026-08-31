# SysHUD

桌面悬浮式系统资源监视器（Windows 10 / 11）。
Desktop floating system resource monitor for Windows 10 / 11.

## 特性 / Features

- 半透明 / 置顶 / 鼠标穿透（点击落到桌面）的"水印式"悬浮窗
- Translucent / always-on-top / click-through (clicks pass to the desktop) "watermark-style" overlay window
- 显示：CPU、内存、网络、多显卡 GPU（逐行）、占用资源前 3 进程
- Shows: CPU, memory, network, multi-GPU (per-card rows), and the top 3 resource-consuming processes
- 系统托盘图标 + 右键菜单（显示/隐藏、设置、置顶开关、退出）
- System tray icon with right-click menu (show/hide, settings, pin toggle, exit)
- 设置界面：位置、透明度、刷新率、显示项开关、开机启动
- Settings dialog: position, opacity, refresh rate, display toggles, auto-start
- 开机启动（HKCU 注册表 Run，静默启动）
- Auto-start via the HKCU Run registry key (silent launch)
- 纯 Win32 / C++ 原生，单 exe，无运行时依赖，体积 ~570KB
- Pure native Win32 / C++, single exe, no runtime dependencies, ~570KB in size

## 编译 / Build

（Visual Studio 2022 Build Tools / MSVC v143）

```bat
msbuild SysHUD.vcxproj /p:Configuration=Release /p:Platform=x64
```

- 输出：`x64\Release\SysHUD.exe`
- Output: `x64\Release\SysHUD.exe`

## 运行 / Run

直接双击 `SysHUD.exe`。首次运行使用内置默认值，配置保存在同目录 `SysHUD.ini`。
Double-click `SysHUD.exe` to launch. The first run uses built-in defaults; configuration is saved to `SysHUD.ini` in the same directory.

托盘右键可打开设置；双击托盘亦可打开设置。
Right-click the tray icon to open settings; double-clicking the tray icon also opens settings.

## 模块 / Modules

| 文件 File | 职责 Responsibility |
|-----------|---------------------|
| main.cpp | 入口、消息循环、模块装配、电源恢复处理 / Entry, message loop, module wiring, power-resume handling |
| OverlayWindow | 分层窗口、透明/穿透/置顶、自绘渲染 / Layered window, transparency/click-through/pin, custom rendering |
| TrayIcon | 托盘图标与右键菜单 / Tray icon and context menu |
| SettingsDlg | 设置对话框 / Settings dialog |
| Sampler | 采集调度（工作线程）/ Sampling scheduler (worker thread) |
| CpuMemNet | CPU/内存/网络（PDH）/ CPU, memory, network (PDH) |
| GpuMonitor | 多显卡 GPU（DXGI 枚举 + D3DKMT，WMI 兜底）/ Multi-GPU (DXGI enum + D3DKMT, WMI fallback) |
| ProcTop | Top3 进程枚举 / Top-3 process enumeration |
| Config | ini 读写 + 开机启动注册表 / ini read/write + autostart registry |

## 备注 / Notes

- GPU 利用率通过运行时 `GetProcAddress` 加载 `gdi32!D3DKMTQueryStatistics`，失败时回退 WMI 性能计数器（单卡）。`D3DKMT` 的 `RunningTime` 单位为 **100ns**（与 FILETIME 一致）。
  GPU utilization is queried via runtime `GetProcAddress` of `gdi32!D3DKMTQueryStatistics`; it falls back to WMI performance counters (single card) on failure. `D3DKMT`'s `RunningTime` is in **100ns** units (same as FILETIME).
- 总体 GPU = 该显卡所有引擎（3D / Copy / Video 等）运行时间增量之和 ÷ 采样间隔；进程级 GPU 取该进程在**所有（显卡 × 引擎）中单引擎运行时间增量的峰值**（与任务管理器进程「GPU」列口径一致），**不跨引擎、不跨显卡累加**，因此进程 GPU 不会超过该显卡总体 GPU。
  Overall GPU = sum of running-time deltas across all engines (3D / Copy / Video …) of that GPU ÷ sample interval. Per-process GPU = **peak** of the process's single-engine running-time delta over all (GPU × engine) pairs (consistent with Task Manager's per-process "GPU" column), **not summed across engines or cards**, so per-process GPU never exceeds the overall GPU of its card.
- 高 DPI：使用 Per-Monitor V2 感知。
  High-DPI: uses Per-Monitor V2 awareness.
- 中文显示：悬浮窗文字使用 Segoe UI，设置对话框标签为英文以避免 .rc 编码问题。
  Chinese rendering: the overlay uses Segoe UI; settings dialog labels are in English to avoid `.rc` encoding issues.
