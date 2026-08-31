#include "CpuMemNet.h"
#include <windows.h>
#include <iphlpapi.h>
#include <memory>

#pragma comment(lib, "iphlpapi.lib")

CpuMemNet::CpuMemNet() {}
CpuMemNet::~CpuMemNet() { Close(); }

bool CpuMemNet::Init() {
    if (m_init) return true;

    // ---- CPU 初始采样（GetSystemTimes）----
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        m_prevIdle   = ((long long)idle.dwHighDateTime << 32) | idle.dwLowDateTime;
        m_prevKernel = ((long long)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
        m_prevUser   = ((long long)user.dwHighDateTime << 32) | user.dwLowDateTime;
    }

    // 首次网络基准（GetIfTable 快照）
    NetRate(Sample{});  // 仅建立 prev 基准，不产出速率
    m_netReady = true;

    m_init = true;
    return true;
}

void CpuMemNet::Close() {
    m_netReady = false;
}

double CpuMemNet::CpuUtil() {
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) return 0.0;
    long long curIdle   = ((long long)idle.dwHighDateTime << 32) | idle.dwLowDateTime;
    long long curKernel = ((long long)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
    long long curUser   = ((long long)user.dwHighDateTime << 32) | user.dwLowDateTime;

    long long idleDiff   = curIdle - m_prevIdle;
    long long kernelDiff = curKernel - m_prevKernel;
    long long userDiff   = curUser - m_prevUser;
    long long totalDiff  = kernelDiff + userDiff;

    double util = 0.0;
    if (totalDiff > 0)
        util = (double)(totalDiff - idleDiff) * 100.0 / (double)totalDiff;

    m_prevIdle = curIdle; m_prevKernel = curKernel; m_prevUser = curUser;
    if (util < 0) util = 0; if (util > 100) util = 100;
    return util;
}

void CpuMemNet::MemInfo(Sample& s) {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        s.memTotalMB = (long long)(ms.ullTotalPhys / (1024 * 1024));
        s.memUsedMB  = (long long)((ms.ullTotalPhys - ms.ullAvailPhys) / (1024 * 1024));
    }
}

void CpuMemNet::NetRate(Sample& s) {
    // 汇总所有网卡（含虚拟网卡）的收发字节累计，做差值得到速率
    MIB_IFTABLE* pTable = nullptr;
    DWORD dwSize = 0;
    s.netDownKbps = 0; s.netUpKbps = 0;

    // 先取所需大小
    if (GetIfTable(nullptr, &dwSize, FALSE) != ERROR_INSUFFICIENT_BUFFER) return;
    pTable = (MIB_IFTABLE*)malloc(dwSize);
    if (!pTable) return;
    if (GetIfTable(pTable, &dwSize, FALSE) != NO_ERROR) { free(pTable); return; }

    unsigned long long inBytes = 0, outBytes = 0;
    for (DWORD i = 0; i < pTable->dwNumEntries; ++i) {
        auto& e = pTable->table[i];
        if (e.dwType == MIB_IF_TYPE_LOOPBACK) continue;        // 跳过回环
        if (e.dwOperStatus != MIB_IF_OPER_STATUS_OPERATIONAL) continue; // 仅统计已连接
        inBytes  += e.dwInOctets;
        outBytes += e.dwOutOctets;
    }
    free(pTable);  // 单一释放点，避免双重释放

    long long now = (long long)GetTickCount64();
    if (!m_netReady) {
        // 仅建立基准，不产出速率
        m_prevBytesIn = inBytes; m_prevBytesOut = outBytes; m_prevNetTick = now;
        return;
    }

    long long dt = now - m_prevNetTick;
    if (dt <= 0) dt = 1;
    double sec = (double)dt / 1000.0;
    double din  = (double)(inBytes  > m_prevBytesIn  ? inBytes  - m_prevBytesIn  : 0);
    double dout = (double)(outBytes > m_prevBytesOut ? outBytes - m_prevBytesOut : 0);

    s.netDownKbps = (din  / sec) * 8.0 / 1000.0;  // bytes/s -> kbps
    s.netUpKbps   = (dout / sec) * 8.0 / 1000.0;

    m_prevBytesIn = inBytes; m_prevBytesOut = outBytes; m_prevNetTick = now;
}

void CpuMemNet::SampleNow(Sample& s) {
    Init();
    s.cpu = CpuUtil();
    MemInfo(s);
    NetRate(s);
}
