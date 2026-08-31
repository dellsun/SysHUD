#pragma once
#include "SysHUD.h"
#include <vector>
#include <string>
#include <windows.h>

// 多显卡 GPU 利用率采集（基于 D3DKMTQueryStatistics，内核级，驱动必提供）
// 按 DXGI 枚举到的每张显卡的 LUID，查询 Adapter 级累计运行时间
// （RunningTime，单位 100ns），用相邻两帧差值 / 系统时间差值 估算利用率。
class GpuMonitor {
public:
    GpuMonitor();
    ~GpuMonitor();

    bool Init();                       // 枚举适配器
    void Close();
    void SampleNow(Sample& s);         // 填充 s.gpus

    // 获取所有显卡的 LUID（供 per-process GPU 统计使用）
    static const std::vector<LUID>& GetLuids();
    // 按 LUID 取显卡名称（纯型号，不含 GPUx- 前缀；找不到返回空串）
    static std::wstring GetAdapterName(const LUID& luid);
    // 按 LUID 取显卡序号（用于拼 GPUx- 前缀；找不到返回 -1）
    static int GetAdapterIndex(const LUID& luid);

private:
    bool m_init = false;
    std::vector<std::wstring> m_adapterNames;
    std::vector<LUID>         m_luid;     // 与 adapterNames 一一对应

    // 每张卡上一帧的累计运行时间（100ns）与系统时间（100ns）
    std::vector<ULONGLONG> m_prevRunningTime;
    ULONGLONG              m_prevSysTime = 0;
};
