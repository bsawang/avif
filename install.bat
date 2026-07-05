@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

:: Check admin / 检查管理员权限
net session >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo Please run as Administrator! / 请以管理员身份运行！
    pause
    exit /b 1
)

echo =============================================
echo  AVIF Thumbnail Handler - Install / 安装
echo =============================================
echo.

:: [1/5] Copy DLL / 复制 DLL
echo [1/5] Installing DLL / 安装 DLL...
copy /y "%~dp0bin\AvifThumbCpp.dll" "%windir%\System32\AvifThumbCpp.dll" >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo   [FAIL] Cannot copy DLL / 无法复制 DLL ^(maybe Explorer is using it^)
    echo          Close all Explorer windows and retry / 关闭所有资源管理器窗口后重试
    pause
    exit /b 1
)
echo   [OK] AvifThumbCpp.dll installed / 安装完成

:: [2/5] Install avifdec.exe / 安装解码器
echo [2/5] Installing avifdec.exe / 安装解码器...
if not exist "%ProgramFiles%\AvifThumbHandler" mkdir "%ProgramFiles%\AvifThumbHandler" >nul 2>&1
copy /y "%~dp0bin\avifdec.exe" "%ProgramFiles%\AvifThumbHandler\avifdec.exe" >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo   [FAIL] Cannot copy avifdec.exe / 无法复制 avifdec.exe
    pause
    exit /b 1
)
echo   [OK] avifdec.exe installed / 安装完成

:: [3/5] COM registration / COM 注册
echo [3/5] Registering COM server / 注册 COM 组件...
reg add "HKLM\SOFTWARE\Classes\CLSID\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}" /ve /t REG_SZ /d "AVIF Thumbnail Provider" /f >nul
reg add "HKLM\SOFTWARE\Classes\CLSID\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}\InprocServer32" /ve /t REG_SZ /d "%windir%\System32\AvifThumbCpp.dll" /f >nul
reg add "HKLM\SOFTWARE\Classes\CLSID\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}\InprocServer32" /v ThreadingModel /t REG_SZ /d "Apartment" /f >nul

:: KEY FIX: DisableProcessIsolation — see RESEARCH.md for details
:: 关键修复：禁用进程隔离 — 详见 RESEARCH.md
reg add "HKLM\SOFTWARE\Classes\CLSID\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}" /v DisableProcessIsolation /t REG_DWORD /d 1 /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved" /v "{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}" /t REG_SZ /d "AVIF Thumbnail Provider" /f >nul
regsvr32 /s "%windir%\System32\AvifThumbCpp.dll"
echo   [OK] COM server registered / 注册完成

:: [4/5] File association (.avif) / 文件关联
echo [4/5] Registering .avif file association / 注册文件关联...
reg add "HKLM\SOFTWARE\Classes\.avif" /ve /t REG_SZ /d "avif_auto_file" /f >nul
reg add "HKLM\SOFTWARE\Classes\.avif" /v ContentType /t REG_SZ /d "image/avif" /f >nul
reg add "HKLM\SOFTWARE\Classes\.avif" /v PerceivedType /t REG_SZ /d "image" /f >nul
reg add "HKLM\SOFTWARE\Classes\.avif\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}" /ve /t REG_SZ /d "{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}" /f >nul
reg add "HKLM\SOFTWARE\Classes\.avif\ShellEx\{BB2E617C-0920-11d1-9A0B-00C04FC2D6C1}" /ve /t REG_SZ /d "{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}" /f >nul
echo   [OK] .avif association done / 关联完成

:: [5/5] Restart Explorer / 重启资源管理器
echo [5/5] Restarting Explorer / 重启资源管理器...
taskkill /f /im explorer.exe >nul 2>&1
timeout /t 2 /nobreak >nul
start explorer.exe
echo   [OK] Explorer restarted / 重启完成

echo.
echo =============================================
echo  Install complete! / 安装完成！
echo.
echo  Open a folder with .avif files to see thumbnails.
echo  打开包含 .avif 文件的文件夹即可查看缩略图。
echo.
echo  Debug log / 调试日志: C:\Windows\Temp\AvifThumbCpp.log
echo =============================================
echo.
pause
