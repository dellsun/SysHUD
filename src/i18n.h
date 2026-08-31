#pragma once
#include <windows.h>

// 语言模式
enum class LangMode { Auto = 0, Chinese = 1, English = 2 };

// 文本键（避免与 winnt.h 的 SID 冲突，命名为 StrId）
enum class StrId {
    // 悬浮窗
    Cpu, Mem, Gpu, Net, Top3Title, Sampling,
    NetDown, NetUp,
    TopByCpu, TopByMem, TopByGpu, TopByNet, TopByMixed,   // 进程 TOP 后缀：CPU/内存/GPU/网络/混合
    LblTopBy,             // 设置项：TOP 排序依据标签
    // 托盘菜单
    TrayShowHide, TraySettings, TrayTopmost, TrayExit,
    // 设置对话框
    DlgTitle, GrpPosition, GrpAppearance, GrpInterval, GrpItems, GrpGpuList, GrpGeneral,
    LblOpacity, LblX, LblY, LblInterval, LblCpu, LblMem, LblGpu, LblNet,
    LblTop3,     LblCharts, LblAutostart, LblLanguage, LblTopCount,
    LblMonitor,
    LblBorder,   LblTopmost,
    BtnApply, BtnReset, BtnClose,
    PosTopLeft, PosTopRight, PosBottomLeft, PosBottomRight, PosCustom,
    Intv500, Intv1000, Intv2000,
    GpuDetecting,
    LangAuto, LangChinese, LangEnglish,
    NetNeedAdmin,   // TOP 网络排序无权限提示
    // 报价（方案 B：中性命名，不含"股票/证券"字眼）
    GrpQuotes, LblQuotes, LblQuoteCodes, LblQuoteInterval,
};

namespace I18N {
    // 依据 g_cfg.lang 确定实际语言并初始化
    void Init();
    bool IsChinese();
    const wchar_t* T(StrId id);
    // 语言下拉项
    const wchar_t* LangOption(int idx);  // idx 0/1/2
    int LangCount();
}
