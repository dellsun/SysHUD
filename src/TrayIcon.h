#pragma once
#include "SysHUD.h"
#include <windows.h>
#include <shellapi.h>
#include <functional>

// 系统托盘图标 + 右键菜单
class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    bool Create(HINSTANCE hInst, HWND hwnd);
    void Remove();

    // 处理 WM_TRAYICON 消息，弹出菜单
    void OnNotify(LPARAM lParam);

    // 菜单命令处理（由 main 提供回调）
    void SetCallbacks(std::function<void()> showHide,
                      std::function<void()> settings,
                      std::function<void()> toggleTop,
                      std::function<void()> exit);

private:
    NOTIFYICONDATAW m_nid{};
    HMENU m_menu = nullptr;
    std::function<void()> m_cbShowHide, m_cbSettings, m_cbToggleTop, m_cbExit;
};
