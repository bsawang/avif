@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

net session >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo Please run as Administrator!
    pause
    exit /b 1
)

echo =============================================
echo  AVIF Thumbnail Handler - Uninstall
echo =============================================
echo.

:: [1/7] Kill processes holding our DLL
echo [1/7] Killing processes...
taskkill /f /im explorer.exe >nul 2>&1
taskkill /f /im dllhost.exe >nul 2>&1
timeout /t 2 /nobreak >nul
echo   [OK] Processes stopped

:: [2/7] Remove DLL
echo [2/7] Removing DLL...
if exist "%windir%\System32\AvifThumbCpp.dll" (
    del /f /q "%windir%\System32\AvifThumbCpp.dll" >nul 2>&1 && echo   [OK] AvifThumbCpp.dll removed || echo   [WARN] Could not delete AvifThumbCpp.dll
)
if exist "%windir%\System32\AvifWIC.dll" (
    del /f /q "%windir%\System32\AvifWIC.dll" >nul 2>&1 && echo   [OK] AvifWIC.dll removed
)

:: [3/7] Remove avifdec.exe (v1.0 compatibility)
echo [3/7] Removing avifdec.exe...
if exist "%ProgramFiles%\AvifThumbHandler\avifdec.exe" (
    del /f /q "%ProgramFiles%\AvifThumbHandler\avifdec.exe" >nul 2>&1
    rmdir "%ProgramFiles%\AvifThumbHandler" >nul 2>&1
    echo   [OK] avifdec.exe and folder removed
) else (
    echo   [SKIP] avifdec.exe not found
)

:: [4/7] Unregister COM server (both v1.0 and current CLSIDs)
echo [4/7] Cleaning COM registrations...

:: Current thumbnail provider
regsvr32 /u /s "%windir%\System32\AvifThumbCpp.dll" >nul 2>&1

:: Remove CLSID registrations
reg delete "HKLM\SOFTWARE\Classes\CLSID\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Classes\CLSID\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0D}" /f >nul 2>&1
echo   [OK] COM registrations cleaned

:: [5/7] Remove .avif shell extension registrations
echo [5/7] Cleaning .avif registrations...
reg delete "HKLM\SOFTWARE\Classes\.avif\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Classes\.avif\ShellEx\{BB2E617C-0920-11d1-9A0B-00C04FC2D6C1}" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers\.avif" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\WIC\Decoders\.avif" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\WIC\Decoders\.avifs" /f >nul 2>&1
echo   [OK] .avif registrations cleaned

:: [6/7] Clean up registry values
echo [6/7] Cleaning registry values...
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved" /v "{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}" /f >nul 2>&1
echo   [OK] Registry values cleaned

:: [7/7] Restart Explorer
echo [7/7] Restarting Explorer...
start explorer.exe
echo   [OK] Explorer restarted

:: Clean up log
del /q "%TEMP%\AvifThumbCpp.log" >nul 2>&1
del /q "C:\Windows\Temp\AvifThumbCpp.log" >nul 2>&1

echo.
echo =============================================
echo  Uninstall complete!
echo.
echo  AVIF thumbnails will no longer show.
echo  System default icons will be used.
echo =============================================
echo.
pause
