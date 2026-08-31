#include "SysHUD.h"
#include "Config.h"
#include "OverlayWindow.h"
#include "TrayIcon.h"
#include "SettingsDlg.h"
#include "Sampler.h"
#include "GpuMonitor.h"
#include "i18n.h"
#include <windows.h>
#include <commctrl.h>
#include <vector>
#include <functional>

// 全局
HINSTANCE g_hInst = nullptr;
AppConfig g_cfg;
Sample    g_sample;
History   g_history;
bool      g_running = true;

HWND g_hOverlay = nullptr;
std::vector<std::wstring> g_gpuNames;

// 模块实例
static OverlayWindow g_overlay;
static TrayIcon      g_tray;
static SettingsDlg   g_settings;
static Sampler       g_sampler;

static LRESULT CALLBACK FrameWndProc(HWND h, UINT m, WPARAM w, LPARAM l);

// 顶层异常捕获：崩溃时弹窗提示，避免“静默退出”
static LONG WINAPI TopLevelFilter(EXCEPTION_POINTERS* ep) {
    wchar_t buf[256];
    swprintf(buf, 256, L"SysHUD 崩溃 (异常 0x%08X)。\n请反馈此信息。",
             ep ? ep->ExceptionRecord->ExceptionCode : 0);
    MessageBoxW(nullptr, buf, L"SysHUD", MB_ICONERROR | MB_OK);
    return EXCEPTION_EXECUTE_HANDLER;
}

int RunApp(HINSTANCE hInst) {
    g_hInst = hInst;

    // 单实例：避免重复运行互相干扰
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, L"Global\\SysHUD_SingleInstance");
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"SysHUD 已在运行", L"SysHUD", MB_OK);
        CloseHandle(hMutex);
        return 0;
    }

    // 高 DPI  awareness
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 公共控件（滑块/列表）
    InitCommonControls();

    // 加载配置
    Config::Load(g_cfg);
    I18N::Init();   // 依据配置/系统语言初始化文本表

    // 先创建不可见的"框架窗口"以接收托盘/定时器消息
    const wchar_t* frameCls = L"SysHUD_FrameCls";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = FrameWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = frameCls;
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"RegisterClassExW 失败", L"SysHUD", MB_ICONERROR);
        return 1;
    }
    HWND frame = CreateWindowExW(0, frameCls, L"SysHUD_Frame", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, nullptr, nullptr, hInst, nullptr);
    if (!frame) {
        MessageBoxW(nullptr, L"创建框架窗口失败", L"SysHUD", MB_ICONERROR);
        return 1;
    }

    // 创建悬浮窗
    if (!g_overlay.Create(hInst)) {
        MessageBoxW(nullptr, L"创建悬浮窗失败（分层窗口/UpdateLayeredWindow）", L"SysHUD", MB_ICONERROR);
        return 1;
    }

    // 创建托盘
    if (!g_tray.Create(hInst, frame)) {
        // 托盘失败不致命，继续运行
    }
    g_tray.SetCallbacks(
        [&]() { g_overlay.Toggle(); },
        [&]() { g_settings.ShowModal(hInst, frame); },
        [&]() { g_cfg.topmost = !g_cfg.topmost; g_overlay.ApplyConfig(); Config::Save(g_cfg); },
        [&]() { g_running = false; PostQuitMessage(0); }
    );

    // 设置对话框应用回调
    g_settings.SetApplyCallback([&]() {
        g_overlay.ApplyConfig();
        g_sampler.SetInterval(g_cfg.interval);
        g_sampler.SetQuoteInterval(g_cfg.quoteInterval);
    });

    // 预检测 GPU 名（用于设置界面预览）
    {
        GpuMonitor gm;
        gm.Init();
        Sample s;
        gm.SampleNow(s);
        for (auto& g : s.gpus) g_gpuNames.push_back(g.name);
        gm.Close();
    }

    // 启动采集线程（首帧会触发重绘）
    g_sampler.Start();

    // 消息循环
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_QUIT) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_sampler.Stop();
    g_tray.Remove();
    if (hMutex) CloseHandle(hMutex);
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    SetUnhandledExceptionFilter(TopLevelFilter);
    int r = 0;
    __try {
        r = RunApp(hInst);
    } __except (TopLevelFilter(GetExceptionInformation())) {
        r = 1;
    }
    return r;
}

LRESULT CALLBACK FrameWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TRAYICON) {
        g_tray.OnNotify(l);
        return 0;
    }
    if (m == WM_POWERBROADCAST && w == PBT_APMRESUMEAUTOMATIC) {
        // 休眠恢复：重新采样一帧基准
        g_sampler.SampleOnce();
        return 0;
    }
    if (m == WM_DESTROY) {
        return DefWindowProcW(h, m, w, l);
    }
    return DefWindowProcW(h, m, w, l);
}
