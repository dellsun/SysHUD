#pragma once
// 必须最先以“完整”形式包含 windows.h（不带 WIN32_LEAN_AND_MEAN），
// 否则 LUID 等类型在部分编译单元下不全。d3dkmthk.h / iphlpapi 仅在 ProcTop.cpp 内包含。
#undef WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "SysHUD.h"
#include <unordered_map>

// 占用资源最多的前 3 个进程（默认按 CPU 使用率）
class ProcTop {
public:
    ProcTop();
    ~ProcTop();

    void Init();
    // count: 输出前 N 名；emit=false 时只更新内部差值基准、不写入 s.top（供 showTop 关闭时保活基准）
    void SampleNow(Sample& s, int count = 3, bool emit = true);

private:
    struct ProcInfo {
        DWORD pid;
        DWORD rootPid = 0;  // 进程树根（主进程）pid，用于聚合子进程
        std::wstring name;
        unsigned long long lastKernel = 0;
        unsigned long long lastUser   = 0;
        double cpu = 0.0;
        unsigned long long memMB = 0;
        double gpu = 0.0;                                 // 当前 GPU 利用率 %
        LUID  gpuLuid = {};                              // 本进程主要使用的 GPU（贡献最大的引擎所在 adapter）
        std::unordered_map<UINT64, ULONGLONG> gpuNodePrev; // 每个 (adapter,node) 上一帧 GPU 累计运行时间（100ns），用于帧间差分
        double netKbps = 0.0;                // 当前网络速率 kbps
        unsigned long long lastNetBytes = 0; // 上一帧网络累计字节
    };
    std::vector<ProcInfo> m_procs;
    bool m_netUnsupported = false;  // 无管理员权限时无法启用逐连接 EStats

    // 用 GetExtendedTcpTable + GetPerTcpConnectionEStats 累计每进程网络字节
    bool CollectNetBytes(std::vector<ProcInfo>& procs);
};
