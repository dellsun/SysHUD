#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <deque>
#include "resource.h"

// 自定义窗口消息（C++ 侧使用，不进 .rc）
#define WM_TRAYICON    (WM_APP + 1)
#define WM_UPDATE_VIEW (WM_APP + 2)

// 历史曲线缓冲长度（点数）
const int HIST_LEN = 60;

// 全局配置结构
struct AppConfig {
    int  posMode   = 3;      // 0=左上 1=右上 2=左下 3=右下 4=自定义
    int  x         = -1;     // 自定义坐标（相对所选显示器工作区，<0 表示自动）
    int  y         = -1;
    int  monitor   = 0;      // 0=主显示器 1..N=第 N 个显示器
    int  alpha     = 70;     // 整体透明度 20..100 (%)
    int  interval  = 1000;   // 采集间隔 ms (500/1000/2000)
    bool showCpu   = true;
    bool showMem   = true;
    bool showGpu   = true;
    bool showNet   = true;
    bool showTop   = true;
    int  topBy     = 4;       // 0=CPU 1=MEM 2=GPU 3=NET 4=混合（进程 TOP 排序依据）
    int  topCount  = 3;       // 进程 TOP 显示条数
    bool showCharts= true;    // 迷你走势图
    int  lang      = 0;       // 0=自动 1=中文 2=英文
    bool autostart = false;
    bool topmost   = true;
    bool showBorder = true;   // 悬浮窗边框线（便于定位窗口范围，可隐藏）
    bool showQuotes = false;   // 是否显示代码报价
    std::wstring quoteCodes = L"sh600519"; // 逗号分隔的报价代码（如 sh600519,sz000001）
    int  quoteInterval = 10000; // 报价刷新间隔 ms
};

// 单条采样数据
struct GpuStat {
    std::wstring name;
    double       usage = 0.0;   // 0..100
};

struct ProcStat {
    std::wstring name;
    double       cpu   = 0.0;   // %
    unsigned long long memMB = 0;
    double       gpu   = 0.0;   // % （聚合所有 GPU 引擎）
    double       netKbps = 0.0; // 网络速率（下行+上行）
    std::wstring gpuName;       // 主要使用的 GPU 名称（GPU 排序时展示）
};

struct Sample {
    double cpu = 0.0;                       // %
    unsigned long long memUsedMB = 0;
    unsigned long long memTotalMB = 0;
    double netDownKbps = 0.0;
    double netUpKbps   = 0.0;
    bool netUnsupported = false;            // 无管理员权限，无法启用逐连接 EStats
    std::vector<GpuStat> gpus;
    std::vector<ProcStat> top;              // 前 3
    std::vector<std::wstring> quoteLines;   // 报价行：代码 + 当前价（由 Sampler 写入，不含中文名）
};

// 历史曲线缓冲（由 Sampler 写入，OverlayWindow 读取绘制）
struct History {
    std::deque<double> cpu;
    std::deque<double> memPct;   // 内存占用百分比
    std::deque<double> net;      // 网络合计 kbps
    std::vector<std::deque<double>> gpus; // 每卡一个序列
    void PushCpu(double v)    { cpu.push_back(v); if (cpu.size() > HIST_LEN) cpu.pop_front(); }
    void PushMem(double v)    { memPct.push_back(v); if (memPct.size() > HIST_LEN) memPct.pop_front(); }
    void PushNet(double v)    { net.push_back(v); if (net.size() > HIST_LEN) net.pop_front(); }
    void ResizeGpus(size_t n) { if (gpus.size() != n) gpus.assign(n, {}); }
    void PushGpu(size_t i, double v) {
        if (i < gpus.size()) { gpus[i].push_back(v); if (gpus[i].size() > HIST_LEN) gpus[i].pop_front(); }
    }
};

// 多显示器辅助：按索引返回显示器矩形（0=主显示器）。workArea=true 返回工作区。
// 返回显示器数量；rc 收到所选显示器矩形。idx 越界时回退主显示器。
int GetMonitorRect(int idx, RECT& rc, bool workArea = true);

// 列出显示器描述（如 "1: 1920x1080"），用于设置下拉框。names 填充，返回数量。
int EnumMonitorNames(std::vector<std::wstring>& names);

// 全局实例（main.cpp 定义）
extern HINSTANCE g_hInst;
extern AppConfig g_cfg;
extern Sample    g_sample;
extern History   g_history;
extern bool      g_running;
