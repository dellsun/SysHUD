#include "Sampler.h"
#include "SysHUD.h"
#include <windows.h>

extern HWND g_hOverlay;

Sampler::Sampler() {
    m_evtStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}
Sampler::~Sampler() {
    Stop();
    if (m_evtStop) CloseHandle(m_evtStop);
}

bool Sampler::Start() {
    if (m_running) return true;
    m_cpuMemNet.Init();
    m_gpu.Init();
    m_procTop.Init();
    ResetEvent(m_evtStop);
    m_running = true;
    m_thread = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
    return m_thread != nullptr;
}

void Sampler::Stop() {
    if (!m_running) return;
    m_running = false;
    if (m_evtStop) SetEvent(m_evtStop);
    if (m_thread) {
        WaitForSingleObject(m_thread, 2000);
        CloseHandle(m_thread);
        m_thread = nullptr;
    }
}

void Sampler::SetInterval(int ms) {
    g_cfg.interval = ms;
}

void Sampler::SetQuoteInterval(int ms) {
    g_cfg.quoteInterval = ms;
    m_quoteAcc = 0; // 立即触发一次刷新
}

void Sampler::FetchQuotes() {
    if (!g_cfg.showQuotes || g_cfg.quoteCodes.empty()) {
        if (!m_quoteLines.empty()) m_quoteLines.clear();
        return;
    }
    // 非 A股交易时段：仅拉取一次（获取收盘值冻结显示），之后不再重复请求
    if (!QuoteFetcher::IsMarketOpen() && !m_quoteLines.empty()) {
        return;
    }
    std::vector<std::wstring> out;
    if (m_quote.Fetch(g_cfg.quoteCodes, out)) {
        m_quoteLines.swap(out);
    }
    // 失败则保留上一次的结果（不清空），避免闪烁
}

void Sampler::SampleOnce() {
    if (!m_cpuMemNet.Init()) return;
    m_gpu.Init();
    m_procTop.Init();
    FetchQuotes();
    Sample s;
    if (g_cfg.showCpu || g_cfg.showMem || g_cfg.showNet) m_cpuMemNet.SampleNow(s);
    if (g_cfg.showGpu) m_gpu.SampleNow(s);
    // 始终更新 TOP 基准（差值计算依赖上一帧），仅在 showTop 时输出到 s.top
    m_procTop.SampleNow(s, g_cfg.showTop ? g_cfg.topCount : 0);
    s.quoteLines = m_quoteLines;
    g_sample = s;
    PushHistory(s);
    if (g_hOverlay) PostMessageW(g_hOverlay, WM_UPDATE_VIEW, 0, 0);
}

void Sampler::PushHistory(const Sample& s) {
    if (g_cfg.showCpu)      g_history.PushCpu(s.cpu);
    if (g_cfg.showMem) {
        double pct = s.memTotalMB ? (double)s.memUsedMB * 100.0 / (double)s.memTotalMB : 0.0;
        g_history.PushMem(pct);
    }
    if (g_cfg.showNet)      g_history.PushNet(s.netDownKbps + s.netUpKbps);
    if (g_cfg.showGpu) {
        g_history.ResizeGpus(s.gpus.size());
        for (size_t i = 0; i < s.gpus.size(); ++i)
            g_history.PushGpu(i, s.gpus[i].usage);
    }
}

DWORD WINAPI Sampler::ThreadProc(LPVOID lp) {
    ((Sampler*)lp)->Loop();
    return 0;
}

void Sampler::Loop() {
    // 立即采一帧（首帧也触发重绘）
    SampleOnce();
    while (m_running) {
        DWORD wait = WaitForSingleObject(m_evtStop, (DWORD)g_cfg.interval);
        if (wait == WAIT_OBJECT_0) break;

        // 报价独立计时（与系统 interval 解耦）
        m_quoteAcc += g_cfg.interval;
        if (m_quoteAcc >= (DWORD)g_cfg.quoteInterval) {
            m_quoteAcc = 0;
            FetchQuotes();
        }

        Sample s;
        if (g_cfg.showCpu || g_cfg.showMem || g_cfg.showNet) m_cpuMemNet.SampleNow(s);
        if (g_cfg.showGpu) m_gpu.SampleNow(s);
        // 始终更新 TOP 基准，仅在 showTop 时输出到 s.top
        m_procTop.SampleNow(s, g_cfg.showTop ? g_cfg.topCount : 0);
        s.quoteLines = m_quoteLines;

        {
            g_sample = s;
            PushHistory(s);   // 持续累积历史，图表才能波动
            if (g_hOverlay) PostMessageW(g_hOverlay, WM_UPDATE_VIEW, 0, 0);
        }
    }
}
