@echo off
setlocal enabledelayedexpansion

REM === Find MSVC ===
set VCVARS=
set VCVARS_ARGS=
for %%P in (
    "C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    "C:\BuildTools\VC\Auxiliary\Build\vcvarsx86_amd64.bat"
    "C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) do (
    if exist %%P (
        set VCVARS=%%~P
        echo %%~nxP | findstr /C:"vcvarsall.bat" >nul && set VCVARS_ARGS=x64
    )
)

if "%VCVARS%"=="" (
    echo ERROR: MSVC 2022 Build Tools not found.
    echo Install from: https://aka.ms/vs/17/release/vs_BuildTools.exe
    echo Select "Desktop development with C++" workload.
    exit /b 1
)

if "%VCVARS_ARGS%"=="" (
    call "%VCVARS%"
) else (
    call "%VCVARS%" %VCVARS_ARGS%
)
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to initialize MSVC environment.
    exit /b 1
)

set SRC=%~dp0
set OUTDIR=%SRC%

REM === Build AvifThumbCpp.dll (IThumbnailProvider) ===
echo === Compiling AvifThumbCpp.dll ===
cl /nologo /LD /MT /O2 /GS- /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_USRDLL" /D "_WINDLL" ^
    /D "_CRT_SECURE_NO_WARNINGS" /EHsc /W4 "%SRC%dllmain.cpp" ^
    /link /SUBSYSTEM:WINDOWS /DLL /MACHINE:x64 /DEF:"%SRC%exports.def" ^
    kernel32.lib user32.lib gdi32.lib gdiplus.lib ole32.lib oleaut32.lib uuid.lib shlwapi.lib advapi32.lib ^
    /OUT:"%OUTDIR%AvifThumbCpp.dll"
if %ERRORLEVEL% NEQ 0 (
    echo AvifThumbCpp.dll compilation FAILED
    exit /b 1
)

REM === Build AvifWIC.dll (WIC BitmapDecoder) ===
echo === Compiling AvifWIC.dll ===
cl /nologo /LD /MT /O2 /GS- /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_USRDLL" /D "_WINDLL" ^
    /D "_CRT_SECURE_NO_WARNINGS" /EHsc /W4 "%SRC%avif_wic2.cpp" ^
    /link /SUBSYSTEM:WINDOWS /DLL /MACHINE:x64 /DEF:"%SRC%exports.def" ^
    kernel32.lib user32.lib gdi32.lib gdiplus.lib ole32.lib oleaut32.lib uuid.lib shlwapi.lib advapi32.lib ^
    /OUT:"%OUTDIR%AvifWIC.dll"
if %ERRORLEVEL% NEQ 0 (
    echo AvifWIC.dll compilation FAILED
    exit /b 1
)

echo === All compilations OK ===
echo   %OUTDIR%AvifThumbCpp.dll
echo   %OUTDIR%AvifWIC.dll
echo.
echo To register: regsvr32 "%OUTDIR%AvifThumbCpp.dll"
echo               regsvr32 "%OUTDIR%AvifWIC.dll"
