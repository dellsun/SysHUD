// 必须最先以“完整”形式包含 windows.h（不带 WIN32_LEAN_AND_MEAN），
// 否则 d3dkmthk.h 依赖的类型/SAL 注解不完整而编译失败。
#undef WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ntstatus.h>
#include <d3dkmthk.h>
#include "GpuMonitor.h"
#include <dxgi1_2.h>
#include <string>
#include <algorithm>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "gdi32.lib")   // D3DKMTQueryStatistics 由 gdi32.dll 导出

GpuMonitor::GpuMonitor() {}
GpuMonitor::~GpuMonitor() { Close(); }

bool GpuMonitor::Init() {
    if (m_init) return true;

    // ---- DXGI 枚举所有 Adapter（取名称与 LUID）----
    // 过滤掉 Microsoft Basic Render Driver（系统自带的软件渲染兜底适配器，非真实硬件）
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) {
        IDXGIAdapter1* adp = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adp) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            adp->GetDesc1(&desc);
            std::wstring name(desc.Description);
            if (name.find(L"Microsoft Basic Render Driver") != std::wstring::npos ||
                name.find(L"Basic Render") != std::wstring::npos) {
                adp->Release();
                adp = nullptr;
                continue;
            }
            m_adapterNames.push_back(name);
            m_luid.push_back(desc.AdapterLuid);
            adp->Release();
            adp = nullptr;
        }
        factory->Release();
    }
    if (m_adapterNames.empty()) {
        m_adapterNames.push_back(L"GPU");
        LUID z{}; m_luid.push_back(z);
    }

    m_prevRunningTime.assign(m_luid.size(), 0);
    m_prevSysTime = 0;
    m_init = true;
    return true;
}

void GpuMonitor::Close() {
    m_adapterNames.clear();
    m_luid.clear();
    m_prevRunningTime.clear();
    m_prevSysTime = 0;
    m_init = false;
}

const std::vector<LUID>& GpuMonitor::GetLuids() {
    static std::vector<LUID> s_luids;
    static bool s_done = false;
    if (!s_done) {
        s_done = true;
        GpuMonitor tmp;
        if (tmp.Init())
            s_luids = tmp.m_luid;
    }
    return s_luids;
}

std::wstring GpuMonitor::GetAdapterName(const LUID& luid) {
    const auto& luids = GetLuids();
    static std::vector<std::wstring> s_names;
    // 重新取一次名字缓存（与 GetLuids 内的静态保持一致）
    static bool s_namesDone = false;
    if (!s_namesDone) {
        s_namesDone = true;
        GpuMonitor tmp;
        if (tmp.Init()) s_names = tmp.m_adapterNames;
    }
    for (size_t i = 0; i < luids.size(); ++i) {
        if (luids[i].HighPart == luid.HighPart && luids[i].LowPart == luid.LowPart)
            return (i < s_names.size()) ? s_names[i] : L"";
    }
    return L"";
}

int GpuMonitor::GetAdapterIndex(const LUID& luid) {
    const auto& luids = GetLuids();
    for (size_t i = 0; i < luids.size(); ++i) {
        if (luids[i].HighPart == luid.HighPart && luids[i].LowPart == luid.LowPart)
            return (int)i;
    }
    return -1;
}

// 查询某 LUID 对应 Adapter 所有节点的累计运行时间之和（100ns）
static bool QueryAdapterRunningTime(const LUID& luid, ULONGLONG& outRunning) {
    outRunning = 0;

    // 先取节点数
    D3DKMT_QUERYSTATISTICS qs{};
    qs.Type = D3DKMT_QUERYSTATISTICS_ADAPTER;
    qs.AdapterLuid = luid;
    if (D3DKMTQueryStatistics(&qs) != 0)
        return false;
    ULONG nodeCount = qs.QueryResult.AdapterInformation.NodeCount;
    if (nodeCount == 0)
        return false;

    ULONGLONG sum = 0;
    for (ULONG n = 0; n < nodeCount; ++n) {
        D3DKMT_QUERYSTATISTICS qsN{};
        qsN.Type = D3DKMT_QUERYSTATISTICS_NODE;
        qsN.AdapterLuid = luid;
        qsN.QueryNode.NodeId = n;
        if (D3DKMTQueryStatistics(&qsN) == 0) {
            sum += (ULONGLONG)qsN.QueryResult.NodeInformation.GlobalInformation.RunningTime.QuadPart;
        }
    }
    outRunning = sum;
    return true;
}

void GpuMonitor::SampleNow(Sample& s) {
    Init();
    s.gpus.clear();

    ULONGLONG sysNow = GetTickCount64() * 10000ULL; // ms -> 100ns
    bool havePrev = (m_prevSysTime != 0);

    for (size_t a = 0; a < m_luid.size(); ++a) {
        GpuStat g;
        // 存纯型号（前缀 GPUx- 在渲染层统一拼，保证截断长度一致）
        g.name = m_adapterNames[a];

        ULONGLONG rt = 0;
        if (QueryAdapterRunningTime(m_luid[a], rt)) {
            if (havePrev && rt >= m_prevRunningTime[a] && sysNow > m_prevSysTime) {
                ULONGLONG dRt  = rt - m_prevRunningTime[a];
                ULONGLONG dSys = sysNow - m_prevSysTime;
                double usage = (double)dRt / (double)dSys * 100.0;
                if (usage < 0) usage = 0; if (usage > 100) usage = 100;
                g.usage = usage;
            } else {
                g.usage = 0.0; // 首帧或时间倒退，跳过
            }
            m_prevRunningTime[a] = rt;
        } else {
            g.usage = 0.0;
            m_prevRunningTime[a] = 0;
        }
        s.gpus.push_back(g);
    }

    m_prevSysTime = sysNow;
}
