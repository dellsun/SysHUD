#pragma once
#include "SysHUD.h"
#include <string>

namespace Config {

std::wstring GetConfigPath();
void Load(AppConfig& cfg);
void Save(const AppConfig& cfg);

// 开机启动（HKCU\...\Run）。返回操作是否成功。
bool SetAutostart(bool enable);
bool IsAutostart();

std::wstring GetExePath();

} // namespace Config
