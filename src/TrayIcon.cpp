#include "TrayIcon.h"
#include "resource.h"
#include "i18n.h"
#include <windows.h>
#include <functional>

TrayIcon::TrayIcon() {}
TrayIcon::~TrayIcon() { Remove(); }

bool TrayIcon::Create(HINSTANCE hInst, HWND hwnd) {
    m_nid.cbSize = sizeof(m_nid);
    m_nid.hWnd = hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = LoadIconW(hInst, MAKEINTRESOURCE(IDI_APPICON));
    wcscpy_s(m_nid.szTip, _countof(m_nid.szTip), L"SysHUD");
    BOOL ok = Shell_NotifyIconW(NIM_ADD, &m_nid);
    if (!ok) return false;

    // 构建右键菜单（按当前语言）
    m_menu = CreatePopupMenu();
    AppendMenuW(m_menu, MF_STRING, ID_TRAY_SHOWHIDE, I18N::T(StrId::TrayShowHide));
    AppendMenuW(m_menu, MF_STRING, ID_TRAY_SETTINGS, I18N::T(StrId::TraySettings));
    AppendMenuW(m_menu, MF_STRING, ID_TRAY_TOPMOST,  I18N::T(StrId::TrayTopmost));
    AppendMenuW(m_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m_menu, MF_STRING, ID_TRAY_EXIT,    I18N::T(StrId::TrayExit));
    return true;
}

void TrayIcon::Remove() {
    if (m_nid.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_nid.hWnd = nullptr;
    }
    if (m_menu) { DestroyMenu(m_menu); m_menu = nullptr; }
}

void TrayIcon::SetCallbacks(std::function<void()> showHide,
                            std::function<void()> settings,
                            std::function<void()> toggleTop,
                            std::function<void()> exit) {
    m_cbShowHide = showHide;
    m_cbSettings = settings;
    m_cbToggleTop = toggleTop;
    m_cbExit = exit;
}

void TrayIcon::OnNotify(LPARAM lParam) {
    if (lParam == WM_RBUTTONUP) {
        POINT p; GetCursorPos(&p);
        SetForegroundWindow(m_nid.hWnd);
        UINT id = TrackPopupMenu(m_menu, TPM_RETURNCMD | TPM_NONOTIFY,
            p.x, p.y, 0, m_nid.hWnd, nullptr);
        switch (id) {
            case ID_TRAY_SHOWHIDE: if (m_cbShowHide) m_cbShowHide(); break;
            case ID_TRAY_SETTINGS: if (m_cbSettings) m_cbSettings(); break;
            case ID_TRAY_TOPMOST:  if (m_cbToggleTop) m_cbToggleTop(); break;
            case ID_TRAY_EXIT:     if (m_cbExit) m_cbExit(); break;
        }
    } else if (lParam == WM_LBUTTONDBLCLK) {
        if (m_cbSettings) m_cbSettings();
    }
}
