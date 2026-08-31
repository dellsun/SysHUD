#pragma once
#include "SysHUD.h"
#include <windows.h>
#include <functional>

// 设置对话框（Win32 Dialog）
class SettingsDlg {
public:
    SettingsDlg();
    ~SettingsDlg();

    // 模态显示；返回 true 表示用户点击了应用/关闭
    int ShowModal(HINSTANCE hInst, HWND owner);

    // 应用回调（main 提供：写配置 + 通知 Overlay/Tray 刷新）
    void SetApplyCallback(std::function<void()> cb) { m_cbApply = cb; }

private:
    static INT_PTR CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    void OnInit(HWND h);
    void InitControls(HWND h);   // 仅填充/刷新控件状态（不创建 trackbar）
    void OnApply(HWND h);
    void OnReset(HWND h);

    std::function<void()> m_cbApply;
    HINSTANCE m_hInst = nullptr;
    HWND m_hwnd = nullptr;   // 当前对话框句柄（防止重复打开）
    bool m_open = false;
};
