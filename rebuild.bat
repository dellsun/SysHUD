@echo off
taskkill /f /im SysHUD.exe >nul 2>&1
ping -n 2 127.0.0.1 >nul
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" SysHUD.vcxproj /p:Configuration=Release /p:Platform=x64 /nologo /v:minimal > build_out.log 2>&1
