#pragma once
#include "SysHUD.h"

// CPU / 内存 / 网络 采集
//  - CPU：GetSystemTimes
//  - 内存：GlobalMemoryStatusEx
//  - 网络：GetIfTable2（IPHelper）按所有网卡总收发字节做差值，无本地化问题、稳定
class CpuMemNet {
public:
    CpuMemNet();
    ~CpuMemNet();

    // 初始化（失败返回 false）
    bool Init();
    void Close();

    // 填充 sample 的 cpu / 内存 / 网络字段（不修改 gpus / top）
    void SampleNow(Sample& s);

private:
    double CpuUtil();
    void   MemInfo(Sample& s);
    void   NetRate(Sample& s);

    bool m_init = false;

    // CPU
    long long m_prevIdle = 0, m_prevKernel = 0, m_prevUser = 0;

    // 网络：累计字节差值法
    unsigned long long m_prevBytesIn  = 0;   // 所有网卡接收字节累计
    unsigned long long m_prevBytesOut = 0;   // 所有网卡发送字节累计
    long long          m_prevNetTick  = 0;   // 上次采样时间戳 (ms)
    bool               m_netReady     = false;
};
