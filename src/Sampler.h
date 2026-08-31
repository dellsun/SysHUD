#pragma once
#include "SysHUD.h"
#include "CpuMemNet.h"
#include "GpuMonitor.h"
#include "ProcTop.h"
#include "QuoteFetcher.h"

// 采集调度器：工作线程按 interval 周期采样，结果写入全局 g_sample，
// 并通过 WM_UPDATE_VIEW 通知主线程重绘。
class Sampler {
public:
    Sampler();
    ~Sampler();

    bool Start();
    void Stop();
    void SetInterval(int ms);   // 热更新间隔
    void SetQuoteInterval(int ms); // 热更新报价刷新间隔

    // 立即采集一帧（供休眠恢复等场景，不阻塞）
    void SampleOnce();

private:
    void PushHistory(const Sample& s);
    void FetchQuotes();         // 拉取代码报价并写入 m_quoteLines

private:
    static DWORD WINAPI ThreadProc(LPVOID lp);
    void Loop();

    HANDLE m_thread = nullptr;
    HANDLE m_evtStop = nullptr;
    volatile bool m_running = false;

    CpuMemNet m_cpuMemNet;
    GpuMonitor m_gpu;
    ProcTop   m_procTop;
    QuoteFetcher m_quote;            // 代码报价获取器
    std::vector<std::wstring> m_quoteLines; // 最近一次报价结果（仅工作线程写）
    DWORD      m_quoteAcc = 0;       // 报价计时累加（ms）
};
