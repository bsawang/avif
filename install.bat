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
echo  AVIF Thumbnail Handler - Install
echo  (libavif inline decode)
echo =============================================
echo.

:: [1/5] Cleanup old components + slow PropertyHandler
echo [1/5] Cleaning up old components...
if exist "%windir%\System32\AvifWIC.dll" (
    echo   Removing old AvifWIC.dll...
    del /f /q "%windir%\System32\AvifWIC.dll" >nul 2>&1
)
reg delete "HKLM\SOFTWARE\Classes\CLSID\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0D}" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\WIC\Decoders\.avif" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\WIC\Decoders\.avifs" /f >nul 2>&1
:: Remove slow PropertyHandler (PhotoMetadataHandler) that causes 6-7s file dialog hangs
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers\.avif" /f >nul 2>&1
echo   [OK] Cleanup done

:: [2/5] Copy DLL
echo [2/5] Installing DLL...
copy /y "%~dp0bin\AvifThumbCpp.dll" "%windir%\System32\AvifThumbCpp.dll" >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo   [FAIL] Cannot copy DLL (maybe Explorer is using it)
    echo          Close all Explorer windows and retry.
    pause
    exit /b 1
)
echo   [OK] AvifThumbCpp.dll installed (libavif inline decode)

:: [3/5] COM registration
echo [3/5] Registering COM server...
reg add "HKLM\SOFTWARE\Classes\CLSID\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}" /ve /t REG_SZ /d "AVIF Thumbnail Provider" /f >nul
reg add "HKLM\SOFTWARE\Classes\CLSID\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}\InprocServer32" /ve /t REG_SZ /d "%windir%\System32\AvifThumbCpp.dll" /f >nul
reg add "HKLM\SOFTWARE\Classes\CLSID\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}\InprocServer32" /v ThreadingModel /t REG_SZ /d "Apartment" /f >nul

:: DisableProcessIsolation for in-process loading
reg add "HKLM\SOFTWARE\Classes\CLSID\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}" /v DisableProcessIsolation /t REG_DWORD /d 1 /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved" /v "{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}" /t REG_SZ /d "AVIF Thumbnail Provider" /f >nul
regsvr32 /s "%windir%\System32\AvifThumbCpp.dll"
echo   [OK] COM server registered

:: [4/5] File association (.avif)
echo [4/5] Registering .avif file association...
reg add "HKLM\SOFTWARE\Classes\.avif" /ve /t REG_SZ /d "avif_auto_file" /f >nul
reg add "HKLM\SOFTWARE\Classes\.avif" /v ContentType /t REG_SZ /d "image/avif" /f >nul
reg add "HKLM\SOFTWARE\Classes\.avif" /v PerceivedType /t REG_SZ /d "image" /f >nul
reg add "HKLM\SOFTWARE\Classes\.avif\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}" /ve /t REG_SZ /d "{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}" /f >nul
reg add "HKLM\SOFTWARE\Classes\.avif\ShellEx\{BB2E617C-0920-11d1-9A0B-00C04FC2D6C1}" /ve /t REG_SZ /d "{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}" /f >nul
echo   [OK] .avif association done

:: [5/5] Restart Explorer
echo [5/5] Restarting Explorer...
taskkill /f /im explorer.exe >nul 2>&1
timeout /t 2 /nobreak >nul
start explorer.exe
echo   [OK] Explorer restarted

echo.
echo =============================================
echo  Install complete!
echo.
echo  Open a folder with .avif files to see thumbnails.
echo  Debug log: %%TEMP%%\AvifThumbCpp.log
echo =============================================
echo.
pause
