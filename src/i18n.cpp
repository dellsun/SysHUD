#include "i18n.h"
#include "SysHUD.h"
#include <windows.h>

namespace I18N {

static bool s_chinese = false;

void Init() {
    if (g_cfg.lang == (int)LangMode::Chinese) {
        s_chinese = true;
    } else if (g_cfg.lang == (int)LangMode::English) {
        s_chinese = false;
    } else {
        // Auto：检测用户 UI 语言
        LANGID lid = GetUserDefaultUILanguage();
        WORD pri = PRIMARYLANGID(lid);
        s_chinese = (pri == LANG_CHINESE);
    }
}

bool IsChinese() { return s_chinese; }

// 字符串表（中 / 英），顺序须与 StrId 一致
static const wchar_t* ZH[] = {
    L"CPU", L"内存", L"GPU", L"网络", L"进程 TOP", L"(采样中)",
    L"↓", L"↑",
    L"CPU", L"内存", L"GPU", L"网络", L"混合",
    L"TOP 排序",
    L"显示/隐藏", L"设置", L"置顶切换", L"退出",
    L"SysHUD 设置", L"位置", L"外观", L"刷新率", L"显示项", L"显卡列表", L"常规",
    L"透明度", L"X", L"Y", L"刷新率", L"CPU", L"内存", L"GPU", L"网络",
    L"前3进程",     L"图表", L"开机启动", L"语言", L"个数",
    L"显示器",
    L"边框线",
    L"置顶",
    L"应用", L"重置", L"关闭",
    L"左上", L"右上", L"左下", L"右下", L"自定义",
    L"500 毫秒", L"1000 毫秒", L"2000 毫秒",
    L"(检测中...)",
    L"自动", L"中文", L"English",
    L"需以管理员身份运行",
    L"代码", L"显示代码", L"代码", L"刷新间隔",
};

static const wchar_t* EN[] = {
    L"CPU", L"MEM", L"GPU", L"NET", L"TOP", L"(sampling)",
    L"D", L"U",
    L"CPU", L"MEM", L"GPU", L"NET", L"Mixed",
    L"TOP by",
    L"Show/Hide", L"Settings", L"Toggle Topmost", L"Exit",
    L"SysHUD Settings", L"Position", L"Appearance", L"Interval", L"Items", L"GPU List", L"General",
    L"Opacity", L"X", L"Y", L"Interval", L"CPU", L"MEM", L"GPU", L"NET",
    L"Top3",     L"Charts", L"Autostart", L"Language", L"Count",
    L"Monitor",
    L"Border",
    L"Topmost",
    L"Apply", L"Reset", L"Close",
    L"Top-Left", L"Top-Right", L"Bottom-Left", L"Bottom-Right", L"Custom",
    L"500 ms", L"1000 ms", L"2000 ms",
    L"(detecting...)",
    L"Auto", L"中文", L"English",
    L"Run as administrator",
    L"Codes", L"Show codes", L"Codes", L"Interval",
};

const wchar_t* T(StrId id) {
    int i = (int)id;
    return s_chinese ? ZH[i] : EN[i];
}

const wchar_t* LangOption(int idx) {
    StrId sid = (idx == 0) ? StrId::LangAuto : (idx == 1) ? StrId::LangChinese : StrId::LangEnglish;
    return T(sid);
}

int LangCount() { return 3; }

} // namespace I18N
