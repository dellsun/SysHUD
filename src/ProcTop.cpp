// 必须先以“完整”形式包含 windows.h，再 include d3dkmthk.h（否则该头解析失败）
#undef WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ntstatus.h>
#include <d3dkmthk.h>
#include <iphlpapi.h>
#include <tcpestats.h>
#include "ProcTop.h"
#include "SysHUD.h"
#include "GpuMonitor.h"
#include <psapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <vector>
#include <unordered_map>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "iphlpapi.lib")

extern AppConfig g_cfg;

ProcTop::ProcTop() {}
ProcTop::~ProcTop() {}

// 查询某进程在所有 (adapter, node) 上的 GPU 运行时间，返回帧间增量最大者（即该进程单引擎峰值占用）
// nodePrev：每个 (adapter,node) 上一帧的累计运行时间，函数内自动更新；outMaxDelta = 最大单引擎增量（100ns）
//           outMainLuid = 产生该峰值的 adapter LUID（用于显示 GPU 名称）
// 采用“最大单引擎占用”而非累加，与任务管理器进程 GPU 列口径一致，且保证进程 GPU <= 总体 GPU。
static bool QueryProcessGpuTime(HANDLE hProcess, const std::vector<LUID>& luids,
                                 std::unordered_map<UINT64, ULONGLONG>& nodePrev,
                                 ULONGLONG& outMaxDelta, LUID& outMainLuid) {
    outMaxDelta = 0;
    LUID zero{}; outMainLuid = zero;
    for (auto& luid : luids) {
        D3DKMT_QUERYSTATISTICS qsA{};
        qsA.Type = D3DKMT_QUERYSTATISTICS_ADAPTER;
        qsA.AdapterLuid = luid;
        if (D3DKMTQueryStatistics(&qsA) != 0) continue;
        ULONG nodeCount = qsA.QueryResult.AdapterInformation.NodeCount;
        UINT64 ak = ((UINT64)(ULONG)luid.HighPart << 32) | (UINT64)(ULONG)luid.LowPart;
        for (ULONG n = 0; n < nodeCount; ++n) {
            D3DKMT_QUERYSTATISTICS qs{};
            qs.Type = D3DKMT_QUERYSTATISTICS_PROCESS_NODE;
            qs.AdapterLuid = luid;
            qs.hProcess = hProcess;
            qs.QueryProcessNode.NodeId = n;
            if (D3DKMTQueryStatistics(&qs) == 0) {
                // RunningTime 单位：100ns（与 FILETIME / D3DKMT / GpuMonitor 一致）
                ULONGLONG rt = (ULONGLONG)qs.QueryResult.ProcessNodeInformation.RunningTime.QuadPart;
                UINT64 key = (ak << 16) | (UINT64)(n & 0xFFFF);
                auto it = nodePrev.find(key);
                if (it != nodePrev.end() && rt >= it->second) {
                    ULONGLONG d = rt - it->second;
                    if (d > outMaxDelta) { outMaxDelta = d; outMainLuid = luid; }
                }
                nodePrev[key] = rt;   // 始终更新；首帧仅建立基准（delta 视为 0）
            }
        }
    }
    return true;
}

void ProcTop::Init() {
    m_procs.clear();
}

void ProcTop::SampleNow(Sample& s, int count, bool emit) {
    DWORD pids[4096];
    DWORD needed = 0;
    if (!EnumProcesses(pids, sizeof(pids), &needed)) return;
    DWORD n = needed / sizeof(DWORD);

    // 始终采集 CPU/GPU/NET 三类累计基准，避免切换排序依据后首帧丢失两帧差值导致数值不准
    bool needGpu = true;
    bool needNet = true;
    const auto& luids = GpuMonitor::GetLuids();

    // 构建 pid -> 父 pid / 名字 映射（用于把子进程资源聚合到主进程）
    std::unordered_map<DWORD, DWORD> ppidOf;
    std::unordered_map<DWORD, std::wstring> nameOf;
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe{};
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(snap, &pe)) {
                do {
                    ppidOf[pe.th32ProcessID] = pe.th32ParentProcessID;
                    nameOf[pe.th32ProcessID] = pe.szExeFile;
                } while (Process32NextW(snap, &pe));
            }
            CloseHandle(snap);
        }
    }
    // 向上回溯到进程树根（主进程）：遇到系统进程/自身/无父/explorer 则停止
    // 说明：explorer.exe 是所有桌面程序的父进程，不应把其子进程并入 explorer 聚合，
    // 因此遇到父为 explorer 时，当前进程自身即作为主进程根。
    auto rootOf = [&](DWORD pid) -> DWORD {
        DWORD cur = pid;
        for (int depth = 0; depth < 64; ++depth) {
            auto it = ppidOf.find(cur);
            if (it == ppidOf.end()) break;       // 无父记录
            DWORD p = it->second;
            if (p == cur) break;                  // 自引用
            if (p == 0 || p == 4) return cur;     // 父为 System/Idle，cur 即根
            // 父为 explorer：当前进程作为独立主进程，不再向上并入
            auto nit = nameOf.find(p);
            if (nit != nameOf.end() && _wcsicmp(nit->second.c_str(), L"explorer.exe") == 0)
                return cur;
            cur = p;
        }
        return cur;
    };

    std::vector<ProcInfo> cur;
    for (DWORD i = 0; i < n; ++i) {
        DWORD pid = pids[i];
        if (pid == 0) continue;
        HANDLE h = OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!h) continue;

        wchar_t name[MAX_PATH] = {0};
        if (!GetModuleFileNameExW(h, nullptr, name, MAX_PATH))
            wcscpy_s(name, MAX_PATH, L"unknown");
        const wchar_t* base = wcsrchr(name, L'\\');
        if (base) memmove(name, base + 1, (wcslen(base + 1) + 1) * sizeof(wchar_t));

        PROCESS_MEMORY_COUNTERS pmc{};
        unsigned long long memMB = 0;
        if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc)))
            memMB = pmc.WorkingSetSize / (1024 * 1024);

        FILETIME c, e, k, u;
        unsigned long long kU = 0, uU = 0;
        if (GetProcessTimes(h, &c, &e, &k, &u)) {
            kU = ((unsigned long long)k.dwHighDateTime << 32) | k.dwLowDateTime;
            uU = ((unsigned long long)u.dwHighDateTime << 32) | u.dwLowDateTime;
        }

        ProcInfo pi;
        pi.pid = pid;
        pi.rootPid = rootOf(pid);
        pi.name = name;
        pi.memMB = memMB;

        // CPU%：与上一帧差值，按逻辑核心数归一化为“占全部 CPU 的百分比”
        // 100ns 单位下，interval 毫秒 = interval * 10000 个 tick；满负载占全部核心时
        // 进程 delta = interval * 10000 * numCores
        static int numCores = (int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        if (numCores <= 0) numCores = 1;
        for (auto& p : m_procs) {
            if (p.pid == pid) {
                unsigned long long dk = kU - p.lastKernel;
                unsigned long long du = uU - p.lastUser;
                double sysTicks = (double)g_cfg.interval * 10000.0 * (double)numCores;
                pi.cpu = (double)(dk + du) / sysTicks * 100.0;
                if (pi.cpu < 0) pi.cpu = 0; if (pi.cpu > 100) pi.cpu = 100;
                pi.gpuNodePrev = p.gpuNodePrev;   // 继承上一帧各 (adapter,node) 的 GPU 运行时间基准
                break;
            }
        }
        pi.lastKernel = kU;
        pi.lastUser = uU;

        // GPU 运行时间累计（仅排序需要时才查，降低开销）
        // 取该进程在所有 (GPU, 引擎) 中 RunningTime 增量“最大者”作为占用（与任务管理器进程 GPU 列一致），
        // 而非跨 GPU/跨引擎累加，从而避免进程 GPU 超过总体 GPU 的失真。
        if (needGpu) {
            ULONGLONG d = 0;
            LUID mainLuid{};
            QueryProcessGpuTime(h, luids, pi.gpuNodePrev, d, mainLuid);
            pi.gpuLuid = mainLuid;
            if (d > 0) {
                // RunningTime 单位：100ns（与 GpuMonitor 口径一致）；interval 毫秒 -> 100ns
                double sysNs = (double)g_cfg.interval * 10000.0;
                pi.gpu = (double)d / sysNs * 100.0;
                if (pi.gpu < 0) pi.gpu = 0; if (pi.gpu > 100) pi.gpu = 100;
            }
        }

        cur.push_back(pi);
        CloseHandle(h);
    }

    // 网络：累计每进程 TCP 字节，与上一帧差值算 kbps
    if (needNet) {
        CollectNetBytes(cur);
        double sysMs = (double)g_cfg.interval;
        for (auto& p : cur) {
            for (auto& prev : m_procs) {
                if (prev.pid == p.pid && prev.lastNetBytes > 0 &&
                    p.lastNetBytes >= prev.lastNetBytes) {
                    double bytes = (double)(p.lastNetBytes - prev.lastNetBytes);
                    p.netKbps = bytes * 8.0 / sysMs / 1000.0;
                    if (p.netKbps < 0) p.netKbps = 0;
                }
            }
        }
    }

    // 单进程模式：不合并父子进程，每个进程独立排名
    struct Item {
        DWORD pid;
        std::wstring name;
        double cpu = 0, gpu = 0, netKbps = 0;
        unsigned long long memMB = 0;
        LUID  gpuLuid = {};
    };
    std::vector<Item> items;
    items.reserve(cur.size());
    for (auto& p : cur) {
        // 过滤掉已退出/无名的进程
        std::wstring nm = p.name;
        if (nm.empty()) {
            auto nit = nameOf.find(p.pid);
            if (nit != nameOf.end()) nm = nit->second;
        }
        Item it;
        it.pid = p.pid;
        it.name = nm;
        it.cpu = p.cpu;
        it.gpu = p.gpu;
        it.netKbps = p.netKbps;
        it.memMB = p.memMB;
        it.gpuLuid = p.gpuLuid;
        items.push_back(std::move(it));
    }
    // 单进程 CPU/GPU 理论上限 100（多核机器单进程可超过按整机占比，这里保留原始计算值）
    for (auto& it : items) {
        if (it.cpu > 100) it.cpu = 100;
        if (it.gpu > 100) it.gpu = 100;
    }

    // 按当前排序依据对单进程排序
    if (g_cfg.topBy == 4) {
        // 混合型：将 CPU/GPU/内存/网络 归一化到 0..1 后加权求和（综合负载评分）
        unsigned long long totalMem = s.memTotalMB ? s.memTotalMB : 1;
        auto scoreOf = [&](const Item& a) {
            double cpuN  = a.cpu / 100.0;
            double gpuN  = a.gpu / 100.0;
            double memN  = (double)a.memMB / (double)totalMem;
            double netN  = std::min(a.netKbps / 5000.0, 1.0); // 5Mbps 视为满负荷
            return 0.25 * cpuN + 0.50 * gpuN + 0.15 * memN + 0.10 * netN;
        };
        std::sort(items.begin(), items.end(),
            [&](const auto& a, const auto& b) { return scoreOf(a) > scoreOf(b); });
    } else if (g_cfg.topBy == 3) {
        std::sort(items.begin(), items.end(),
            [](const auto& a, const auto& b) { return a.netKbps > b.netKbps; });
    } else if (g_cfg.topBy == 2) {
        std::sort(items.begin(), items.end(),
            [](const auto& a, const auto& b) { return a.gpu > b.gpu; });
    } else if (g_cfg.topBy == 1) {
        std::sort(items.begin(), items.end(),
            [](const auto& a, const auto& b) { return a.memMB > b.memMB; });
    } else {
        std::sort(items.begin(), items.end(),
            [](const auto& a, const auto& b) { return a.cpu > b.cpu; });
    }

    s.netUnsupported = m_netUnsupported;
    m_procs = std::move(cur);   // 始终更新差值基准（即使 emit=false）

    if (!emit) return;          // showTop 关闭：仅保活基准，不输出到 s.top

    s.top.clear();
    for (size_t i = 0; i < items.size() && i < (size_t)count; ++i) {
        const Item& a = items[i];
        ProcStat ps;
        ps.name = a.name;       // 渲染层对空名兜底
        ps.cpu = a.cpu;
        ps.memMB = a.memMB;
        ps.gpu = a.gpu;
        ps.netKbps = a.netKbps;
        // 仅显示 GPU{i}（不显示型号）
        int gpuIdx = GpuMonitor::GetAdapterIndex(a.gpuLuid);
        ps.gpuName = (gpuIdx >= 0 ? (L"GPU" + std::to_wstring(gpuIdx)) : L"GPU");
        s.top.push_back(ps);
    }
}

// 用 GetExtendedTcpTable + GetPerTcpConnectionEStats 累计每进程网络字节
// 用 GetExtendedTcpTable + GetPerTcpConnectionEStats 累计每进程网络字节。
// 需先以 SetPerTcpConnectionEStats 启用统计（Rw.EnableCollection=TRUE），
// 再以 GetPerTcpConnectionEStats 读取 Rod(TCP_ESTATS_DATA_ROD_v0) 的
// DataBytesIn / DataBytesOut。
typedef DWORD(WINAPI* PFN_SetPerTcpConnectionEStats)(
    PMIB_TCPROW_OWNER_PID Row, TCP_ESTATS_TYPE EstatsType,
    PVOID Rw, ULONG RwVersion, ULONG RwSize, PVOID Ros, ULONG RosVersion, ULONG RosSize);
typedef DWORD(WINAPI* PFN_GetPerTcpConnectionEStats)(
    PMIB_TCPROW_OWNER_PID Row, TCP_ESTATS_TYPE EstatsType,
    PVOID Rw, ULONG RwVersion, ULONG RwSize,
    PVOID Ros, ULONG RosVersion, ULONG RosSize,
    PVOID Rod, ULONG RodVersion, ULONG RodSize);

bool ProcTop::CollectNetBytes(std::vector<ProcInfo>& procs) {
    static PFN_SetPerTcpConnectionEStats spcs = nullptr;
    static PFN_GetPerTcpConnectionEStats gpcs = nullptr;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        HMODULE h = GetModuleHandleW(L"iphlpapi.dll");
        if (h) {
            spcs = (PFN_SetPerTcpConnectionEStats)GetProcAddress(h, "SetPerTcpConnectionEStats");
            gpcs = (PFN_GetPerTcpConnectionEStats)GetProcAddress(h, "GetPerTcpConnectionEStats");
        }
    }
    if (!gpcs || !spcs) return false;

    DWORD size = 0;
    if (GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET,
            TCP_TABLE_OWNER_PID_CONNECTIONS, 0) != ERROR_INSUFFICIENT_BUFFER)
        return false;
    std::vector<BYTE> buf(size);
    PMIB_TCPTABLE_OWNER_PID tab = (PMIB_TCPTABLE_OWNER_PID)buf.data();
    if (GetExtendedTcpTable(tab, &size, FALSE, AF_INET,
            TCP_TABLE_OWNER_PID_CONNECTIONS, 0) != NO_ERROR)
        return false;

    TCP_ESTATS_DATA_RW_v0 rw{};
    rw.EnableCollection = TRUE;
    TCP_ESTATS_DATA_ROD_v0 rod{};
    // 启用统计需在普通进程上具备管理员权限；若被拒绝（ERROR_ACCESS_DENIED），
    // 无法获取逐连接累计字节，直接放弃本次统计（避免返回无意义数据）。
    // 先对首个 ESTAB 连接尝试启用，判断是否有权限。
    bool canEnable = false;
    for (DWORD i = 0; i < tab->dwNumEntries; ++i) {
        if (tab->table[i].dwState == MIB_TCP_STATE_ESTAB) {
            DWORD sr = spcs(&tab->table[i], TcpConnectionEstatsData, &rw, 0, sizeof(rw), nullptr, 0, 0);
            canEnable = (sr == NO_ERROR);
            break;
        }
    }
    if (!canEnable) {
        m_netUnsupported = true;
        return false;
    }
    m_netUnsupported = false;

    for (DWORD i = 0; i < tab->dwNumEntries; ++i) {
        if (tab->table[i].dwState != MIB_TCP_STATE_ESTAB) continue;
        DWORD pid = tab->table[i].dwOwningPid;

        // 读取累计字节
        if (gpcs(&tab->table[i], TcpConnectionEstatsData, nullptr, 0, 0,
                nullptr, 0, 0, &rod, 0, sizeof(rod)) == NO_ERROR) {
            ULONGLONG bytes = rod.DataBytesIn + rod.DataBytesOut;
            for (auto& p : procs)
                if (p.pid == pid) { p.lastNetBytes += bytes; break; }
        }
    }
    return true;
}
