#include "SettingsDlg.h"
#include "SysHUD.h"
#include "Config.h"
#include "GpuMonitor.h"
#include "i18n.h"
#include "resource.h"
#include <windows.h>
#include <string>
#include <vector>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

extern AppConfig g_cfg;
extern std::vector<std::wstring> g_gpuNames;

static SettingsDlg* g_dlg = nullptr;

SettingsDlg::SettingsDlg() {}
SettingsDlg::~SettingsDlg() {}

int SettingsDlg::ShowModal(HINSTANCE hInst, HWND owner) {
    // 防止重复打开：若已存在则激活已有窗口并返回
    if (m_open && m_hwnd) {
        SetForegroundWindow(m_hwnd);
        return 0;
    }
    m_hInst = hInst;
    g_dlg = this;
    return (int)DialogBoxParamW(hInst, MAKEINTRESOURCE(IDD_SETTINGS),
        owner, Proc, (LPARAM)this);
}

void SettingsDlg::OnInit(HWND h) {
    m_hwnd = h;
    m_open = true;
    // 首次打开：创建动态控件（trackbar），并填充其余控件状态
    {
        RECT rcSlider{ 60, 122, 60 + 170, 122 + 16 }; // DLU 矩形（Appearance 组内 Opacity 行）
        MapDialogRect(h, &rcSlider);
        HWND sl = CreateWindowExW(0, L"msctls_trackbar32", nullptr,
            WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | WS_TABSTOP,
            rcSlider.left, rcSlider.top,
            rcSlider.right - rcSlider.left, rcSlider.bottom - rcSlider.top,
            h, (HMENU)(INT_PTR)IDC_ALPHA_SLIDER, m_hInst, nullptr);
        if (sl) {
            SendMessageW(sl, TBM_SETRANGE, TRUE, MAKELPARAM(20, 100));
            SendMessageW(sl, TBM_SETPOS, TRUE, g_cfg.alpha);
        }
    }
    InitControls(h);
}

// 仅填充/刷新各控件状态（不创建 trackbar，供 OnReset 复用）
void SettingsDlg::InitControls(HWND h) {
    HWND lc = GetDlgItem(h, IDC_LANG_COMBO);
    for (int i = 0; i < I18N::LangCount(); ++i)
        SendMessageW(lc, CB_ADDSTRING, 0, (LPARAM)I18N::LangOption(i));
    SendMessageW(lc, CB_SETCURSEL, g_cfg.lang, 0);

    // ---- 按语言覆盖所有文本（.rc 中写英文，运行时统一改）----
    SetWindowTextW(h, I18N::T(StrId::DlgTitle));
    SetDlgItemTextW(h, IDC_GRP_POSITION,   I18N::T(StrId::GrpPosition));
    SetDlgItemTextW(h, IDC_GRP_APPEARANCE, I18N::T(StrId::GrpAppearance));
    SetDlgItemTextW(h, IDC_GRP_INTERVAL,   I18N::T(StrId::GrpInterval));
    SetDlgItemTextW(h, IDC_GRP_ITEMS,      I18N::T(StrId::GrpItems));
    SetDlgItemTextW(h, IDC_GRP_GPULIST,    I18N::T(StrId::GrpGpuList));
    SetDlgItemTextW(h, IDC_GRP_GENERAL,    I18N::T(StrId::GrpGeneral));
    SetDlgItemTextW(h, IDC_LBL_OPACITY,    I18N::T(StrId::LblOpacity));
    SetDlgItemTextW(h, IDC_LBL_LANG,       I18N::T(StrId::LblLanguage));
    SetDlgItemTextW(h, IDC_LBL_MONITOR,    I18N::T(StrId::LblMonitor));

    // 位置下拉
    HWND cb = GetDlgItem(h, IDC_POS_COMBO);
    const wchar_t* items[] = { I18N::T(StrId::PosTopLeft), I18N::T(StrId::PosTopRight),
                               I18N::T(StrId::PosBottomLeft), I18N::T(StrId::PosBottomRight),
                               I18N::T(StrId::PosCustom) };
    for (int i = 0; i < 5; ++i) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)items[i]);
    SendMessageW(cb, CB_SETCURSEL, g_cfg.posMode, 0);
    SetDlgItemInt(h, IDC_X_EDIT, g_cfg.x < 0 ? 0 : g_cfg.x, FALSE);
    SetDlgItemInt(h, IDC_Y_EDIT, g_cfg.y < 0 ? 0 : g_cfg.y, FALSE);

    // 显示器下拉（多显示器）
    HWND mc = GetDlgItem(h, IDC_MONITOR_COMBO);
    std::vector<std::wstring> mons;
    int nMon = EnumMonitorNames(mons);
    for (auto& s : mons) SendMessageW(mc, CB_ADDSTRING, 0, (LPARAM)s.c_str());
    SendMessageW(mc, CB_SETCURSEL, (g_cfg.monitor >= 0 && g_cfg.monitor < nMon) ? g_cfg.monitor : 0, 0);

    // 透明度滑块已在 OnInit 中创建，这里仅同步显示值
    SetDlgItemTextW(h, IDC_ALPHA_LABEL, (std::to_wstring(g_cfg.alpha) + L"%").c_str());

    // 刷新率
    HWND iv = GetDlgItem(h, IDC_INTERVAL_COMBO);
    const wchar_t* ivs[] = { I18N::T(StrId::Intv500), I18N::T(StrId::Intv1000), I18N::T(StrId::Intv2000) };
    for (int i = 0; i < 3; ++i) SendMessageW(iv, CB_ADDSTRING, 0, (LPARAM)ivs[i]);
    int sel = 0;
    if (g_cfg.interval == 500) sel = 0; else if (g_cfg.interval == 2000) sel = 2; else sel = 1;
    SendMessageW(iv, CB_SETCURSEL, sel, 0);

    // 显示项
    CheckDlgButton(h, IDC_CHK_CPU, g_cfg.showCpu ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(h, IDC_CHK_MEM, g_cfg.showMem ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(h, IDC_CHK_GPU, g_cfg.showGpu ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(h, IDC_CHK_NET, g_cfg.showNet ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(h, IDC_CHK_TOP, g_cfg.showTop ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(h, IDC_CHK_CHARTS, g_cfg.showCharts ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(h, IDC_CHK_AUTOSTART, g_cfg.autostart ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(h, IDC_CHK_BORDER, g_cfg.showBorder ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(h, IDC_CHK_TOPMOST, g_cfg.topmost ? BST_CHECKED : BST_UNCHECKED);
    // 复选框文本也需要本地化（覆盖 .rc 英文）
    SetDlgItemTextW(h, IDC_CHK_CPU, I18N::T(StrId::LblCpu));
    SetDlgItemTextW(h, IDC_CHK_MEM, I18N::T(StrId::LblMem));
    SetDlgItemTextW(h, IDC_CHK_GPU, I18N::T(StrId::LblGpu));
    SetDlgItemTextW(h, IDC_CHK_NET, I18N::T(StrId::LblNet));
    SetDlgItemTextW(h, IDC_CHK_TOP, I18N::T(StrId::LblTop3));
    SetDlgItemTextW(h, IDC_CHK_CHARTS, I18N::T(StrId::LblCharts));
    // TOP 排序依据：CPU / MEM / GPU / NET
    SetDlgItemTextW(h, IDC_LBL_TOPBY, I18N::T(StrId::LblTopBy));
    SetDlgItemTextW(h, IDC_LBL_TOPCOUNT, I18N::T(StrId::LblTopCount));
    HWND tb = GetDlgItem(h, IDC_TOPBY_COMBO);
    SendMessageW(tb, CB_ADDSTRING, 0, (LPARAM)I18N::T(StrId::TopByCpu));
    SendMessageW(tb, CB_ADDSTRING, 0, (LPARAM)I18N::T(StrId::TopByMem));
    SendMessageW(tb, CB_ADDSTRING, 0, (LPARAM)I18N::T(StrId::TopByGpu));
    SendMessageW(tb, CB_ADDSTRING, 0, (LPARAM)I18N::T(StrId::TopByNet));
    SendMessageW(tb, CB_ADDSTRING, 0, (LPARAM)I18N::T(StrId::TopByMixed));
    SendMessageW(tb, CB_SETCURSEL, (WPARAM)g_cfg.topBy, 0);
    // TOP 显示条数
    SetDlgItemInt(h, IDC_TOPCOUNT_EDIT, (UINT)g_cfg.topCount, FALSE);
    SetDlgItemTextW(h, IDC_CHK_AUTOSTART, I18N::T(StrId::LblAutostart));
    SetDlgItemTextW(h, IDC_CHK_BORDER, I18N::T(StrId::LblBorder));
    SetDlgItemTextW(h, IDC_CHK_TOPMOST, I18N::T(StrId::LblTopmost));
    SetDlgItemTextW(h, IDC_BTN_APPLY, I18N::T(StrId::BtnApply));
    SetDlgItemTextW(h, IDC_BTN_RESET, I18N::T(StrId::BtnReset));
    SetDlgItemTextW(h, IDC_BTN_CLOSE, I18N::T(StrId::BtnClose));

    // ---- 代码报价（中性命名，不含"股票/证券"字眼）----
    SetDlgItemTextW(h, IDC_GRP_QUOTES,         I18N::T(StrId::GrpQuotes));
    SetDlgItemTextW(h, IDC_CHK_QUOTES,         I18N::T(StrId::LblQuotes));
    SetDlgItemTextW(h, IDC_LBL_QUOTE_CODES,    I18N::T(StrId::LblQuoteCodes));
    SetDlgItemTextW(h, IDC_LBL_QUOTE_INTERVAL, I18N::T(StrId::LblQuoteInterval));
    CheckDlgButton(h, IDC_CHK_QUOTES, g_cfg.showQuotes ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(h, IDC_QUOTE_CODES_EDIT, g_cfg.quoteCodes.c_str());
    HWND qiv = GetDlgItem(h, IDC_QUOTE_INTERVAL_COMBO);
    const wchar_t* qivs[] = { L"5 s", L"10 s", L"30 s", L"60 s" };
    for (int i = 0; i < 4; ++i) SendMessageW(qiv, CB_ADDSTRING, 0, (LPARAM)qivs[i]);
    int qsel = 1;
    switch (g_cfg.quoteInterval) {
        case 5000:  qsel = 0; break;
        case 10000: qsel = 1; break;
        case 30000: qsel = 2; break;
        case 60000: qsel = 3; break;
        default:    qsel = 1; break;
    }
    SendMessageW(qiv, CB_SETCURSEL, qsel, 0);

    // GPU 列表预览（带占用率）
    HWND lv = GetDlgItem(h, IDC_GPU_LIST);
    if (g_sample.gpus.empty()) {
        if (g_gpuNames.empty())
            SendMessageW(lv, LB_ADDSTRING, 0, (LPARAM)I18N::T(StrId::GpuDetecting));
        else
            for (auto& n : g_gpuNames)
                SendMessageW(lv, LB_ADDSTRING, 0, (LPARAM)n.c_str());
    } else {
        size_t idx = 0;
        for (auto& g : g_sample.gpus) {
            std::wstring line = L"GPU" + std::to_wstring(idx) + L" - " + g.name +
                                L"  (" + std::to_wstring((int)(g.usage + 0.5)) + L"%)";
            SendMessageW(lv, LB_ADDSTRING, 0, (LPARAM)line.c_str());
            ++idx;
        }
    }
}

void SettingsDlg::OnApply(HWND h) {
    g_cfg.lang = (int)SendMessageW(GetDlgItem(h, IDC_LANG_COMBO), CB_GETCURSEL, 0, 0);
    g_cfg.posMode = (int)SendMessageW(GetDlgItem(h, IDC_POS_COMBO), CB_GETCURSEL, 0, 0);
    g_cfg.monitor = (int)SendMessageW(GetDlgItem(h, IDC_MONITOR_COMBO), CB_GETCURSEL, 0, 0);
    g_cfg.x = (int)GetDlgItemInt(h, IDC_X_EDIT, nullptr, FALSE);
    g_cfg.y = (int)GetDlgItemInt(h, IDC_Y_EDIT, nullptr, FALSE);
    g_cfg.alpha = (int)SendMessageW(GetDlgItem(h, IDC_ALPHA_SLIDER), TBM_GETPOS, 0, 0);

    int isel = (int)SendMessageW(GetDlgItem(h, IDC_INTERVAL_COMBO), CB_GETCURSEL, 0, 0);
    g_cfg.interval = (isel == 0 ? 500 : (isel == 2 ? 2000 : 1000));

    g_cfg.showCpu = IsDlgButtonChecked(h, IDC_CHK_CPU) == BST_CHECKED;
    g_cfg.showMem = IsDlgButtonChecked(h, IDC_CHK_MEM) == BST_CHECKED;
    g_cfg.showGpu = IsDlgButtonChecked(h, IDC_CHK_GPU) == BST_CHECKED;
    g_cfg.showNet = IsDlgButtonChecked(h, IDC_CHK_NET) == BST_CHECKED;
    g_cfg.showTop = IsDlgButtonChecked(h, IDC_CHK_TOP) == BST_CHECKED;
    g_cfg.topBy = (int)SendMessageW(GetDlgItem(h, IDC_TOPBY_COMBO), CB_GETCURSEL, 0, 0);
    // TOP 显示条数（限制在 1~20）
    int tc = (int)GetDlgItemInt(h, IDC_TOPCOUNT_EDIT, nullptr, FALSE);
    if (tc < 1) tc = 1; if (tc > 20) tc = 20;
    g_cfg.topCount = tc;
    g_cfg.showCharts = IsDlgButtonChecked(h, IDC_CHK_CHARTS) == BST_CHECKED;
    g_cfg.showBorder = IsDlgButtonChecked(h, IDC_CHK_BORDER) == BST_CHECKED;
    g_cfg.topmost = IsDlgButtonChecked(h, IDC_CHK_TOPMOST) == BST_CHECKED;

    // 代码报价
    g_cfg.showQuotes = IsDlgButtonChecked(h, IDC_CHK_QUOTES) == BST_CHECKED;
    {
        wchar_t buf[1024] = { 0 };
        GetDlgItemTextW(h, IDC_QUOTE_CODES_EDIT, buf, 1024);
        g_cfg.quoteCodes = buf;
    }
    int qsel = (int)SendMessageW(GetDlgItem(h, IDC_QUOTE_INTERVAL_COMBO), CB_GETCURSEL, 0, 0);
    g_cfg.quoteInterval = (qsel == 0 ? 5000 : (qsel == 2 ? 30000 : (qsel == 3 ? 60000 : 10000)));

    bool autoWant = IsDlgButtonChecked(h, IDC_CHK_AUTOSTART) == BST_CHECKED;
    if (autoWant != g_cfg.autostart) {
        Config::SetAutostart(autoWant);
        g_cfg.autostart = autoWant;
    }

    Config::Save(g_cfg);
    I18N::Init();  // 重新应用语言
    if (m_cbApply) m_cbApply();
}

void SettingsDlg::OnReset(HWND h) {
    g_cfg = AppConfig{};   // 恢复默认
    // 重新填充各控件状态（注意：trackbar 已存在，不要在 OnInit 里重复创建）
    InitControls(h);
    Config::Save(g_cfg);
    I18N::Init();
    if (m_cbApply) m_cbApply();
}

INT_PTR CALLBACK SettingsDlg::Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_INITDIALOG:
            g_dlg->OnInit(h);
            return TRUE;
        case WM_HSCROLL:
            if (LOWORD(w) == TB_ENDTRACK || LOWORD(w) == TB_THUMBPOSITION || LOWORD(w) == TB_LINEUP || LOWORD(w) == TB_LINEDOWN) {
                int a = (int)SendMessageW(GetDlgItem(h, IDC_ALPHA_SLIDER), TBM_GETPOS, 0, 0);
                SetDlgItemTextW(h, IDC_ALPHA_LABEL, (std::to_wstring(a) + L"%").c_str());
            }
            return TRUE;
        case WM_COMMAND: {
            WORD id = LOWORD(w);
            WORD code = HIWORD(w);
            // 手动切换复选框（BS_CHECKBOX 不自动切换）
            // 注意：仅匹配复选框 ID，不能包含 Apply/Reset 按钮（216/217 在区间内）
            bool isCheckbox = (id >= IDC_CHK_CPU && id <= IDC_CHK_AUTOSTART) || id == IDC_CHK_BORDER || id == IDC_CHK_TOPMOST || id == IDC_CHK_QUOTES;
            if (code == BN_CLICKED && isCheckbox) {
                CheckDlgButton(h, id,
                    IsDlgButtonChecked(h, id) == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED);
                return TRUE;
            }
            switch (id) {
                case IDC_BTN_APPLY: g_dlg->OnApply(h); return TRUE;
                case IDC_BTN_RESET: g_dlg->OnReset(h);  return TRUE;
                case IDC_BTN_CLOSE: EndDialog(h, 1);    return TRUE;
            }
            return FALSE;
        }
        case WM_CLOSE:
            EndDialog(h, 0); return TRUE;
        case WM_DESTROY:
            g_dlg->m_open = false;
            g_dlg->m_hwnd = nullptr;
            return FALSE;
    }
    return FALSE;
}
