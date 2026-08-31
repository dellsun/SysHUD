#include "SysHUD.h"
#include <windows.h>
#include <vector>

namespace {
struct MonCtx {
    std::vector<HMONITOR> prim;   // 主显示器优先
    std::vector<HMONITOR> others;
};

BOOL CALLBACK MonEnum(HMONITOR hm, HDC, LPRECT, LPARAM lp) {
    MonCtx* c = (MonCtx*)lp;
    MONITORINFO mi{ sizeof(mi) };
    if (GetMonitorInfoW(hm, &mi)) {
        if (mi.dwFlags & MONITORINFOF_PRIMARY) c->prim.push_back(hm);
        else c->others.push_back(hm);
    }
    return TRUE;
}
} // namespace

int GetMonitorRect(int idx, RECT& rc, bool workArea) {
    MonCtx c;
    EnumDisplayMonitors(nullptr, nullptr, MonEnum, (LPARAM)&c);
    std::vector<HMONITOR> all;
    for (auto h : c.prim)   all.push_back(h);
    for (auto h : c.others) all.push_back(h);

    int n = (int)all.size();
    if (n == 0) {  // 极端情况：回退到主屏幕
        rc.left = rc.top = 0;
        rc.right = GetSystemMetrics(SM_CXSCREEN);
        rc.bottom = GetSystemMetrics(SM_CYSCREEN);
        return 1;
    }
    if (idx < 0 || idx >= n) idx = 0;  // 越界回退主显示器
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(all[idx], &mi);
    rc = workArea ? mi.rcWork : mi.rcMonitor;
    return n;
}

int EnumMonitorNames(std::vector<std::wstring>& names) {
    MonCtx c;
    EnumDisplayMonitors(nullptr, nullptr, MonEnum, (LPARAM)&c);
    std::vector<HMONITOR> all;
    for (auto h : c.prim)   all.push_back(h);
    for (auto h : c.others) all.push_back(h);

    names.clear();
    int n = (int)all.size();
    for (int i = 0; i < n; ++i) {
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(all[i], &mi);
        int w = mi.rcMonitor.right - mi.rcMonitor.left;
        int h = mi.rcMonitor.bottom - mi.rcMonitor.top;
        wchar_t buf[64];
        if (i == 0 && (mi.dwFlags & MONITORINFOF_PRIMARY))
            wsprintfW(buf, L"1: %dx%d (Primary)", w, h);
        else
            wsprintfW(buf, L"%d: %dx%d", i + 1, w, h);
        names.push_back(buf);
    }
    return n;
}
