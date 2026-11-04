@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul
echo ========================================================
echo   SlideNoteFormatter v2.0 (Tauri + Tailwind UI) Build
echo ========================================================
echo.

cd /d "%~dp0"

if exist "C:\msys64\mingw64\bin\gcc.exe" (
    set "PATH=C:\msys64\mingw64\bin;%USERPROFILE%\.cargo\bin;%PATH%"
) else (
    set "PATH=%USERPROFILE%\.cargo\bin;%PATH%"
)

where cargo >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Rust cargo compiler not found in PATH!
    if "%~1"=="/nopause" goto :eof
    pause
    exit /b 1
)

where npm >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Node.js npm not found in PATH!
    if "%~1"=="/nopause" goto :eof
    pause
    exit /b 1
)

if not exist "dist" mkdir "dist"

echo [1/3] Building Tailwind UI + React frontend...
call npm.cmd run build
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Frontend build failed!
    if "%~1"=="/nopause" goto :eof
    pause
    exit /b 1
)

echo.
echo [2/3] Compiling Tauri v2 (Rust) executable (Release)...
cd src-tauri
cargo build --release
if %ERRORLEVEL% neq 0 (
    echo [WARNING] Release build failed, trying debug build...
    cargo build
    if %ERRORLEVEL% neq 0 (
        echo [ERROR] Rust build failed!
        cd ..
        if "%~1"=="/nopause" goto :eof
        pause
        exit /b 1
    )
    set "BIN_SRC=target\debug\slide-note-formatter.exe"
    set "DLL_SRC=target\debug\WebView2Loader.dll"
) else (
    set "BIN_SRC=target\release\slide-note-formatter.exe"
    set "DLL_SRC=target\release\WebView2Loader.dll"
)
cd ..

echo.
echo [3/3] Copying binaries and dependencies to dist\...
copy /Y "src-tauri\%BIN_SRC%" "dist\SlideNoteFormatter.exe" >nul
if exist "src-tauri\%DLL_SRC%" (
    copy /Y "src-tauri\%DLL_SRC%" "dist\WebView2Loader.dll" >nul
)
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to copy binary to dist folder!
    if "%~1"=="/nopause" goto :eof
    pause
    exit /b 1
)

echo.
echo ========================================================
echo   BUILD COMPLETED SUCCESSFULLY!
echo   Output: dist\SlideNoteFormatter.exe
for %%I in ("dist\SlideNoteFormatter.exe") do (
    set /a size_mb=%%~zI/1048576
    echo   Size: %%~zI bytes (~!size_mb! MB)
)
echo ========================================================
echo.
if "%~1"=="/nopause" goto :eof
pause
