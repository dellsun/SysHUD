#include "OverlayWindow.h"
#include "SysHUD.h"
#include "i18n.h"
#include <windows.h>
#include <string>
#include <algorithm>
#include <malloc.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")

extern HWND g_hOverlay;

// 取 CPU 型号（注册表，轻量稳定）
static std::wstring GetCpuModel() {
    static std::wstring cached;
    static bool done = false;
    if (done) return cached;
    done = true;
    HKEY hk;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS) {
        wchar_t buf[256] = {};
        DWORD sz = sizeof(buf);
        if (RegQueryValueExW(hk, L"ProcessorNameString", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS) {
            cached = buf;
            // 去掉多余空格
            size_t p = cached.find_last_not_of(L' ');
            if (p != std::wstring::npos) cached = cached.substr(0, p + 1);
        }
        RegCloseKey(hk);
    }
    if (cached.empty()) cached = L"CPU";
    return cached;
}

// 取活动网络接口名（取第一个有有效 IPv4 的以太网/无线适配器描述）
// 名称过长时截断并加省略号（仅截设备型号部分，保留前缀与数值）
static const size_t NAME_MAX = 20;   // 设备型号统一截断长度
static std::wstring TruncName(const std::wstring& s, size_t max) {
    if (s.size() <= max) return s;
    return s.substr(0, max) + L"\u2026";
}
// 截断并补齐到 NAME_MAX 宽度，使各资源行名称槽等宽、数值对齐
static std::wstring PadName(const std::wstring& s) {
    std::wstring t = TruncName(s, NAME_MAX);
    if (t.size() < NAME_MAX) t.append(NAME_MAX - t.size(), L' ');
    return t;
}
// 资源前缀（CPU/内存/GPUx/网络）补齐到统一宽度，使分隔符对齐
static const size_t PREFIX_W = 4;   // GPU0 / CPU 中最宽的列宽
static std::wstring PadPrefix(const std::wstring& s) {
    if (s.size() >= PREFIX_W) return s;
    return s + std::wstring(PREFIX_W - s.size(), L' ');
}

// 窄串转宽串
static std::wstring MbToW(const char* s) {
    if (!s) return L"";
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (n <= 0) return L"";
    std::wstring r(n - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, &r[0], n);
    return r;
}

static std::wstring GetActiveNetName() {
    // 仅用 GetAdaptersInfo：选有 IPv4 且（有默认网关优先）的以太网/无线适配器描述
    ULONG sz = 0;
    if (GetAdaptersInfo(nullptr, &sz) != ERROR_BUFFER_OVERFLOW) return L"";
    auto* ai = (IP_ADAPTER_INFO*)malloc(sz);
    if (!ai) return L"";
    std::wstring best, fallback;
    if (GetAdaptersInfo(ai, &sz) == NO_ERROR) {
        for (auto* p = ai; p; p = p->Next) {
            if (p->Type != MIB_IF_TYPE_ETHERNET && p->Type != 71 /*IEEE80211*/) continue;
            if (strcmp(p->IpAddressList.IpAddress.String, "0.0.0.0") == 0) continue;
            if (_strnicmp(p->Description, "MS TCP Loopback", 15) == 0) continue;
            std::wstring desc = MbToW(p->Description);
            if (desc.empty()) continue;
            bool hasGateway = (strcmp(p->GatewayList.IpAddress.String, "0.0.0.0") != 0);
            if (hasGateway) { best = desc; break; }
            if (fallback.empty()) fallback = desc;
        }
    }
    free(ai);
    return best.empty() ? fallback : best;
}

OverlayWindow::OverlayWindow() {}
OverlayWindow::~OverlayWindow() {}

static OverlayWindow* g_self = nullptr;

bool OverlayWindow::Create(HINSTANCE hInst) {
    g_self = this;
    const wchar_t* cls = L"SysHUD_OverlayCls";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);

    // 分层 + 透明(鼠标穿透) + 置顶 + 不在任务栏
    DWORD ex = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    m_hwnd = CreateWindowExW(ex, cls, L"SysHUD",
        WS_POPUP, 0, 0, m_w, m_h,
        nullptr, nullptr, hInst, nullptr);
    if (!m_hwnd) return false;
    g_hOverlay = m_hwnd;

    ApplyConfig();
    Show();
    return true;
}

void OverlayWindow::ApplyConfig() {
    if (!m_hwnd) return;
    // 定位逻辑放到 Render() 末尾的 PositionWindow()（基于真实 m_w/m_h），
    // 这里只需触发一次重绘即可。
    Redraw();
}

void OverlayWindow::PositionWindow() {
    if (!m_hwnd) return;

    // ---- 位置（支持多显示器，使用已确定的真实窗口尺寸 m_w/m_h）----
    RECT mr;
    GetMonitorRect(g_cfg.monitor, mr, true);  // 所选显示器工作区
    int mw = mr.right - mr.left;
    int mh = mr.bottom - mr.top;
    int margin = 12;

    int x = mr.left + margin, y = mr.top + margin;
    switch (g_cfg.posMode) {
        case 0: x = mr.left + margin;            y = mr.top + margin;            break;
        case 1: x = mr.left + mw - m_w - margin; y = mr.top + margin;            break;
        case 2: x = mr.left + margin;            y = mr.top + mh - m_h - margin; break;
        case 3: x = mr.left + mw - m_w - margin; y = mr.top + mh - m_h - margin; break;
        default:case 4:  // 自定义：x/y 为相对工作区左上角的坐标
            x = mr.left + (g_cfg.x >= 0 ? g_cfg.x : margin);
            y = mr.top  + (g_cfg.y >= 0 ? g_cfg.y : margin);
            break;
    }
    SetWindowPos(m_hwnd, g_cfg.topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
        x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void OverlayWindow::Show() { if (m_hwnd) { m_visible = true; ShowWindow(m_hwnd, SW_SHOWNOACTIVATE); } }
void OverlayWindow::Hide() { if (m_hwnd) { m_visible = false; ShowWindow(m_hwnd, SW_HIDE); } }
void OverlayWindow::Toggle() { m_visible ? Hide() : Show(); }

void OverlayWindow::Redraw() {
    if (m_hwnd) Render();
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_UPDATE_VIEW) {
        g_self->Render();
        return 0;
    }
    if (m == WM_DESTROY) { return DefWindowProcW(h, m, w, l); }
    return DefWindowProcW(h, m, w, l);
}

void OverlayWindow::Render() {
    // ---- 计算内容尺寸，动态调整窗口 ----
    int lineH  = 16;   // 文字行高
    int chartH = 22;   // 图表高度
    int pad    = 10;
    int blocks = 0;    // 含图表的块数
    int textRows = 0;  // 纯文本行数（Top3 等）

    if (g_cfg.showCpu) blocks++;
    if (g_cfg.showMem) blocks++;
    if (g_cfg.showGpu) blocks += (int)g_sample.gpus.size() ? (int)g_sample.gpus.size() : 1;
    if (g_cfg.showNet) blocks++;
    if (g_cfg.showTop) textRows += (int)g_sample.top.size() ? (int)g_sample.top.size() + 1 : 1;
    if (g_cfg.showQuotes) textRows += (int)g_sample.quoteLines.size();

    int chartUnit = (g_cfg.showCharts && blocks > 0) ? (lineH + chartH) : lineH;
    // 显示 TOP 时，标题上方额外留 8px 间隙（与 Render 中 y += 8 对应）
    int topGap = (g_cfg.showTop || g_cfg.showQuotes) ? 8 : 0;
    m_h = pad * 2 + blocks * chartUnit + textRows * lineH + topGap;
    m_w = 240;
    SetWindowPos(m_hwnd, nullptr, 0, 0, m_w, m_h, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    PositionWindow();   // 用真实 m_w/m_h 定位（修复未定尺寸导致的偏移）

    // ---- 内存 DC 双缓冲 ----
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, m_w, m_h);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
    RECT rc{0, 0, m_w, m_h};
    FillRect(mem, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH)); // 全黑作为透明色键

    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(180, 220, 255));
    HFONT font = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT oldF = (HFONT)SelectObject(mem, font);

    int y = pad;
    auto fmt = [](double v, int d = 1) {
        wchar_t buf[64]; swprintf(buf, 64, L"%.*f", d, v); return std::wstring(buf);
    };

    // 绘制单个图表块（文字 + 波浪面积图）
    auto chartBlock = [&](const std::wstring& label, const std::deque<double>& data, double maxV, const std::wstring& val = L"") {
        TextOutW(mem, pad, y, label.c_str(), (int)label.size());
        // 数值段右对齐到窗口右侧（比例字体下空格填充无法对齐，必须像素定点）
        if (!val.empty()) {
            SIZE sz; GetTextExtentPoint32W(mem, val.c_str(), (int)val.size(), &sz);
            int vx = m_w - pad - sz.cx;
            if (vx < pad + (int)label.size() * 6) vx = pad + (int)label.size() * 6;
            TextOutW(mem, vx, y, val.c_str(), (int)val.size());
        }
        y += lineH;
        if (g_cfg.showCharts && !data.empty()) {
            // 图表占满内容宽度（与文字左缘对齐、右缘贴边），避免右侧空隙
            int gw = m_w - pad * 2;
            int gx = pad, gy = y, gh = chartH;
            // 图表区域背景色（比纯黑略亮，随整体 alpha 半透明）
            RECT rcChart{ gx, gy, gx + gw, gy + gh };
            HBRUSH bg = CreateSolidBrush(RGB(22, 30, 34));
            FillRect(mem, &rcChart, bg);
            DeleteObject(bg);
            // 自适应上限：固定 max 用给定值；否则取序列最大值
            double top = maxV;
            if (top <= 0) {
                top = 1e-6;
                for (double v : data) if (v > top) top = v;
            }

            int n = (int)data.size();
            // 构建波形点（心电图/监护仪风格：连续折线 + 基线，无面积填充）
            POINT* pts = (POINT*)_malloca(n * sizeof(POINT));
            if (pts) {
                for (int i = 0; i < n; ++i) {
                    double v = data[i];
                    if (v > top) v = top;
                    if (v < 0) v = 0;
                    pts[i].x = gx + (int)((double)i * gw / (double)(HIST_LEN - 1));
                    pts[i].y = gy + gh - 1 - (int)(v / top * (gh - 2));
                }

                // 波浪线颜色随紧张度变化：绿(宽松)→黄(中等)→红(紧张)
                // ratio = 当前值 / 上限
                double ratio = data.back() / top;
                if (ratio > 1) ratio = 1; if (ratio < 0) ratio = 0;
                COLORREF wave;
                if (ratio < 0.6) {
                    // 绿 -> 黄
                    int t = (int)(ratio / 0.6 * 255);
                    wave = RGB(t, 255, 90);
                } else {
                    // 黄 -> 红
                    int t = (int)((1.0 - (ratio - 0.6) / 0.4) * 255);
                    wave = RGB(255, t, 60);
                }
                // 辉光（暗色同色，1px）
                HPEN gp = CreatePen(PS_SOLID, 1,
                    RGB(GetRValue(wave) / 2, GetGValue(wave) / 2, GetBValue(wave) / 2));
                HPEN ogp = (HPEN)SelectObject(mem, gp);
                MoveToEx(mem, pts[0].x, pts[0].y, nullptr);
                for (int i = 1; i < n; ++i) LineTo(mem, pts[i].x, pts[i].y);
                SelectObject(mem, ogp); DeleteObject(gp);
                // 主线（亮色，1px）
                HPEN ep = CreatePen(PS_SOLID, 1, wave);
                HPEN oep = (HPEN)SelectObject(mem, ep);
                MoveToEx(mem, pts[0].x, pts[0].y, nullptr);
                for (int i = 1; i < n; ++i) LineTo(mem, pts[i].x, pts[i].y);
                SelectObject(mem, oep); DeleteObject(ep);

                _freea(pts);
            }
            y += chartH;
        }
    };

    if (g_cfg.showCpu)
        chartBlock(PadPrefix(I18N::T(StrId::Cpu)) + L" - " + PadName(GetCpuModel()),
                   g_history.cpu, 100.0, fmt(g_sample.cpu) + L"%");
    if (g_cfg.showMem) {
        double usedG = (double)g_sample.memUsedMB / 1024.0;
        double totalG = (double)g_sample.memTotalMB / 1024.0;
        wchar_t buf[64];
        swprintf(buf, 64, L"%.1f/%.1f G", usedG, totalG);
        chartBlock(PadPrefix(I18N::T(StrId::Mem)) + L" - ", g_history.memPct, 100.0, buf);
    }
    if (g_cfg.showGpu) {
        if (g_sample.gpus.empty())
            chartBlock(PadPrefix(L"GPU0") + L" - ", {}, 100.0, L"--");
        for (size_t i = 0; i < g_sample.gpus.size(); ++i) {
            // 纯型号截 NAME_MAX 并补齐，前缀 GPU{i} 也补齐到统一宽
            std::wstring lab = PadPrefix(L"GPU" + std::to_wstring(i)) + L" - " + PadName(g_sample.gpus[i].name);
            std::wstring val = fmt(g_sample.gpus[i].usage) + L"%";
            std::deque<double> d = (i < g_history.gpus.size()) ? g_history.gpus[i] : std::deque<double>{};
            chartBlock(lab, d, 100.0, val);
        }
    }
    if (g_cfg.showNet) {
        std::wstring netName = GetActiveNetName();
        std::wstring netLabel = PadPrefix(I18N::T(StrId::Net)) + L" - " +
            (netName.empty() ? L"" : TruncName(netName, 14));  // 网卡名截短，避免挤到右侧长数值
        std::wstring netVal = I18N::T(StrId::NetDown) + fmt(g_sample.netDownKbps / 1000.0, 1) + L" " +
            I18N::T(StrId::NetUp) + fmt(g_sample.netUpKbps / 1000.0, 1) + L" Mbps";
        chartBlock(netLabel, g_history.net, 0.0, netVal);  // 0 = 自适应（按当前最大）
    }

    if (g_cfg.showTop) {
        // 与上方图表/内容留出间隙
        y += 8;
        // 标题：进程 TOP · <资源>
        const wchar_t* resSuffix = I18N::T(StrId::TopByCpu);
        if (g_cfg.topBy == 1) resSuffix = I18N::T(StrId::TopByMem);
        else if (g_cfg.topBy == 2) resSuffix = I18N::T(StrId::TopByGpu);
        else if (g_cfg.topBy == 3) resSuffix = I18N::T(StrId::TopByNet);
        else if (g_cfg.topBy == 4) resSuffix = I18N::T(StrId::TopByMixed);
        std::wstring title = std::wstring(I18N::T(StrId::Top3Title)) + L" · " + resSuffix;
        TextOutW(mem, pad, y, title.c_str(), (int)title.size()); y += lineH;
        // 无管理员权限时无法统计逐进程网络，给出提示
        if (g_cfg.topBy == 3 && g_sample.netUnsupported) {
            const wchar_t* hint = I18N::T(StrId::NetNeedAdmin);
            TextOutW(mem, pad, y, hint, (int)wcslen(hint)); y += lineH;
        }
        if (g_sample.top.empty()) {
            TextOutW(mem, pad, y, I18N::T(StrId::Sampling), (int)wcslen(I18N::T(StrId::Sampling))); y += lineH;
        }
        for (auto& p : g_sample.top) {
            std::wstring nm = p.name;
            if (nm.size() > 14) nm = nm.substr(0, 14);
            // 仅显示当前排序资源的数值与单位，避免信息过载
            std::wstring val;
            if (g_cfg.topBy == 1)
                val = std::to_wstring(p.memMB) + L" MB";
            else if (g_cfg.topBy == 2)
                val = fmt(p.gpu) + L"%";
            else if (g_cfg.topBy == 3)
                val = fmt(p.netKbps / 1000.0, 1) + L" Mbps";
            else if (g_cfg.topBy == 4) {
                // 混合型：每个进程只显示其占用最高的那一项资源（内存/GPU/网络/CPU），用对应格式
                double totalMem = g_sample.memTotalMB ? (double)g_sample.memTotalMB : 1.0;
                double cpuN = p.cpu / 100.0, gpuN = p.gpu / 100.0;
                double memN = (double)p.memMB / totalMem;
                double netN = std::min(p.netKbps / 5000.0, 1.0);
                if (memN >= gpuN && memN >= cpuN && memN >= netN)
                    val = std::to_wstring(p.memMB) + L" MB";
                else if (gpuN >= cpuN && gpuN >= netN)
                    val = fmt(p.gpu, 0) + L"%  [" + (p.gpuName.empty() ? L"GPU" : p.gpuName) + L"]";
                else if (netN >= cpuN)
                    val = fmt(p.netKbps / 1000.0, 1) + L" Mbps";
                else
                    val = fmt(p.cpu, 0) + L"%";
            } else
                val = fmt(p.cpu) + L"%";
            // GPU 排序时附带该进程主要使用的 GPU 名称（带 GPUx- 前缀）
            std::wstring extra;
            if (g_cfg.topBy == 2 && !p.gpuName.empty())
                extra = L"  [" + p.gpuName + L"]";
            std::wstring line = nm + L"  " + val + extra;
            TextOutW(mem, pad, y, line.c_str(), (int)line.size());
            y += lineH;
        }
    }

    // ---- 代码（仅显示代码后3位 + 当前价，不含中文名）----
    if (g_cfg.showQuotes) {
        y += 8; // 与上方内容留间隙（与高度计算中的 topGap 对应）
        if (g_sample.quoteLines.empty()) {
            SetTextColor(mem, RGB(180, 220, 255));
            const wchar_t* ph = L"---  ---";
            TextOutW(mem, pad, y, ph, (int)wcslen(ph)); y += lineH;
        }
        for (auto& ql : g_sample.quoteLines) {
            // 行格式： "sh600900  12.34"（代码与现价之间含空白）
            // 取最后一个非空白之后的部分为现价，之前的部分为代码
            size_t p = ql.find_last_not_of(L" \t");
            std::wstring price, code;
            if (p != std::wstring::npos) {
                size_t s = ql.find_last_of(L" \t", p);
                price = ql.substr(s + 1, p - s);
                code = (s == std::wstring::npos) ? ql.substr(0, p + 1) : ql.substr(0, s);
            } else {
                code = ql;
            }
            // 去掉代码首尾空白，避免尾随空格影响长度
            size_t ca = code.find_first_not_of(L" \t");
            size_t cb = code.find_last_not_of(L" \t");
            if (ca != std::wstring::npos) code = code.substr(ca, cb - ca + 1);
            // 仅显示后 3 位
            if (code.size() > 3) code = code.substr(code.size() - 3);
            SetTextColor(mem, RGB(180, 220, 255));
            TextOutW(mem, pad, y, code.c_str(), (int)code.size());
            if (!price.empty()) {
                SetTextColor(mem, RGB(255, 255, 255));
                SIZE sz; GetTextExtentPoint32W(mem, price.c_str(), (int)price.size(), &sz);
                int vx = m_w - pad - sz.cx;
                if (vx < pad + 20) vx = pad + 20;
                TextOutW(mem, vx, y, price.c_str(), (int)price.size());
            }
            y += lineH;
        }
    }

    // ---- 边框线（便于定位悬浮窗范围，可由设置隐藏）----
    // 注意：黑色为透明色键，边框必须用非黑色绘制才会透出。
    if (g_cfg.showBorder) {
        RECT rcBorder{ 0, 0, m_w - 1, m_h - 1 };
        HPEN bp = CreatePen(PS_SOLID, 1, RGB(120, 160, 200));
        HPEN obp = (HPEN)SelectObject(mem, bp);
        HBRUSH obr = (HBRUSH)SelectObject(mem, GetStockObject(NULL_BRUSH));
        Rectangle(mem, rcBorder.left, rcBorder.top, rcBorder.right, rcBorder.bottom);
        SelectObject(mem, obr);
        SelectObject(mem, obp);
        DeleteObject(bp);
    }

    SelectObject(mem, oldF);
    DeleteObject(font);

    // ---- 通过 UpdateLayeredWindow 输出（黑底透明 + 整体 alpha）----
    POINT ptSrc{0, 0};
    POINT ptDst;
    GetWindowRect(m_hwnd, &rc);
    ptDst.x = rc.left; ptDst.y = rc.top;
    SIZE sz{ m_w, m_h };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, (BYTE)(g_cfg.alpha * 255 / 100), 0 };
    COLORREF key = RGB(0, 0, 0);
    // ULW_ALPHA 让整体 SourceConstantAlpha（滑块透明度）生效；ULW_COLORKEY 让纯黑背景完全透明
    UpdateLayeredWindow(m_hwnd, screen, &ptDst, &sz, mem, &ptSrc, key, &bf, ULW_ALPHA | ULW_COLORKEY);

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}
