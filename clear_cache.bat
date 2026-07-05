@echo off
chcp 65001 >nul
title 清理缩略图缓存
net session >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo 请以管理员身份运行！
    pause
    exit /b 1
)

echo [1/3] 停止资源管理器...
taskkill /f /im explorer.exe >nul 2>&1
timeout /t 2 /nobreak >nul

echo [2/3] 删除缩略图缓存...
del /f /s /q "%LocalAppData%\Microsoft\Windows\Explorer\thumbcache_*.db" >nul 2>&1
del /f /s /q "%LocalAppData%\Microsoft\Windows\Explorer\thumbcache_*.idx" >nul 2>&1
del /f /s /q "%LocalAppData%\Microsoft\Windows\Explorer\iconcache_*.db" >nul 2>&1
echo   [OK] 缓存已清理

echo [3/3] 启动资源管理器...
start explorer.exe

echo.
echo 完成！请打开 AVIF 文件夹检查缩略图。
echo.
pause
