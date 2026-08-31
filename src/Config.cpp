#include "Config.h"
#include <windows.h>
#include <shlwapi.h>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")

namespace Config {

std::wstring GetExePath() {
    wchar_t buf[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, n);
}

std::wstring GetConfigPath() {
    std::wstring p = GetExePath();
    size_t pos = p.find_last_of(L"\\/");
    if (pos != std::wstring::npos) p = p.substr(0, pos + 1);
    p += L"SysHUD.ini";
    return p;
}

static int  ReadInt(const wchar_t* path, const wchar_t* key, int def) {
    return GetPrivateProfileIntW(L"SysHUD", key, def, path);
}
static void WriteInt(const wchar_t* path, const wchar_t* key, int v) {
    wchar_t val[32];
    wsprintfW(val, L"%d", v);
    WritePrivateProfileStringW(L"SysHUD", key, val, path);
}
static bool ReadBool(const wchar_t* path, const wchar_t* key, bool def) {
    return ReadInt(path, key, def ? 1 : 0) != 0;
}
static void WriteBool(const wchar_t* path, const wchar_t* key, bool v) {
    WriteInt(path, key, v ? 1 : 0);
}
static std::wstring ReadStr(const wchar_t* path, const wchar_t* key, const std::wstring& def) {
    wchar_t buf[1024] = {0};
    DWORD n = GetPrivateProfileStringW(L"SysHUD", key, def.c_str(), buf, 1024, path);
    return std::wstring(buf, n);
}
static void WriteStr(const wchar_t* path, const wchar_t* key, const std::wstring& v) {
    WritePrivateProfileStringW(L"SysHUD", key, v.c_str(), path);
}

void Load(AppConfig& cfg) {
    std::wstring path = GetConfigPath();
    cfg.posMode   = ReadInt(path.c_str(), L"posMode", cfg.posMode);
    cfg.x         = ReadInt(path.c_str(), L"x", cfg.x);
    cfg.y         = ReadInt(path.c_str(), L"y", cfg.y);
    cfg.monitor   = ReadInt(path.c_str(), L"monitor", cfg.monitor);
    cfg.alpha     = ReadInt(path.c_str(), L"alpha", cfg.alpha);
    cfg.interval  = ReadInt(path.c_str(), L"interval", cfg.interval);
    cfg.showCpu   = ReadBool(path.c_str(), L"showCpu", cfg.showCpu);
    cfg.showMem   = ReadBool(path.c_str(), L"showMem", cfg.showMem);
    cfg.showGpu   = ReadBool(path.c_str(), L"showGpu", cfg.showGpu);
    cfg.showNet   = ReadBool(path.c_str(), L"showNet", cfg.showNet);
    cfg.showTop   = ReadBool(path.c_str(), L"showTop", cfg.showTop);
    cfg.topBy     = ReadInt(path.c_str(), L"topBy", cfg.topBy);
    cfg.topCount  = ReadInt(path.c_str(), L"topCount", cfg.topCount);
    cfg.showCharts= ReadBool(path.c_str(), L"showCharts", cfg.showCharts);
    cfg.lang      = ReadInt(path.c_str(), L"lang", cfg.lang);
    cfg.autostart = IsAutostart();
    cfg.topmost   = ReadBool(path.c_str(), L"topmost", cfg.topmost);
    cfg.showBorder= ReadBool(path.c_str(), L"showBorder", cfg.showBorder);
    cfg.showQuotes= ReadBool(path.c_str(), L"showQuotes", cfg.showQuotes);
    cfg.quoteCodes= ReadStr(path.c_str(), L"quoteCodes", cfg.quoteCodes);
    cfg.quoteInterval = ReadInt(path.c_str(), L"quoteInterval", cfg.quoteInterval);

    if (cfg.alpha < 20) cfg.alpha = 20;
    if (cfg.alpha > 100) cfg.alpha = 100;
    if (cfg.interval != 500 && cfg.interval != 1000 && cfg.interval != 2000)
        cfg.interval = 1000;
}

void Save(const AppConfig& cfg) {
    std::wstring path = GetConfigPath();
    WriteInt(path.c_str(), L"posMode", cfg.posMode);
    WriteInt(path.c_str(), L"x", cfg.x);
    WriteInt(path.c_str(), L"y", cfg.y);
    WriteInt(path.c_str(), L"monitor", cfg.monitor);
    WriteInt(path.c_str(), L"alpha", cfg.alpha);
    WriteInt(path.c_str(), L"interval", cfg.interval);
    WriteBool(path.c_str(), L"showCpu", cfg.showCpu);
    WriteBool(path.c_str(), L"showMem", cfg.showMem);
    WriteBool(path.c_str(), L"showGpu", cfg.showGpu);
    WriteBool(path.c_str(), L"showNet", cfg.showNet);
    WriteBool(path.c_str(), L"showTop", cfg.showTop);
    WriteInt(path.c_str(), L"topBy", cfg.topBy);
    WriteInt(path.c_str(), L"topCount", cfg.topCount);
    WriteBool(path.c_str(), L"showCharts", cfg.showCharts);
    WriteInt(path.c_str(), L"lang", cfg.lang);
    WriteBool(path.c_str(), L"topmost", cfg.topmost);
    WriteBool(path.c_str(), L"showBorder", cfg.showBorder);
    WriteBool(path.c_str(), L"showQuotes", cfg.showQuotes);
    WriteStr(path.c_str(), L"quoteCodes", cfg.quoteCodes);
    WriteInt(path.c_str(), L"quoteInterval", cfg.quoteInterval);
}

bool SetAutostart(bool enable) {
    HKEY hKey;
    LONG r = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey);
    if (r != ERROR_SUCCESS) return false;

    if (enable) {
        std::wstring exe = L"\"" + GetExePath() + L"\"";
        r = RegSetValueExW(hKey, L"SysHUD", 0, REG_SZ,
            (const BYTE*)exe.c_str(), (DWORD)((exe.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hKey, L"SysHUD");
    }
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

bool IsAutostart() {
    HKEY hKey;
    LONG r = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_QUERY_VALUE, &hKey);
    if (r != ERROR_SUCCESS) return false;
    wchar_t buf[512] = {0};
    DWORD sz = sizeof(buf);
    r = RegQueryValueExW(hKey, L"SysHUD", nullptr, nullptr, (BYTE*)buf, &sz);
    RegCloseKey(hKey);
    if (r != ERROR_SUCCESS || sz == 0) return false;
    std::wstring cur = GetExePath();
    std::wstring val(buf);
    if (!val.empty() && val.front() == L'"') val = val.substr(1);
    if (!val.empty() && val.back() == L'"') val = val.substr(0, val.size() - 1);
    return _wcsicmp(val.c_str(), cur.c_str()) == 0;
}

} // namespace Config
