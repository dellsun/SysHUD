#pragma once
#include "SysHUD.h"
#include <windows.h>

// 分层悬浮窗：半透明 / 置顶 / 鼠标穿透 / 水印式自绘
class OverlayWindow {
public:
    OverlayWindow();
    ~OverlayWindow();

    bool Create(HINSTANCE hInst);
    void Show();
    void Hide();
    void Toggle();
    bool IsVisible() const { return m_visible; }

    void ApplyConfig();           // 依据 g_cfg 更新位置/透明度/置顶
    void Redraw();                // 重新渲染内容

    HWND GetHwnd() const { return m_hwnd; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void Render();
    void PositionWindow();      // 依据 g_cfg + 当前 m_w/m_h 定位窗口（尺寸确定后调用）

    HWND m_hwnd = nullptr;
    bool m_visible = true;
    int  m_w = 220, m_h = 200;    // 窗口尺寸（随内容调整）
};
