@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul
echo ========================================================
echo   SlideNoteFormatter - Ultra-Lightweight C/Win32 Build
echo   Target Size: ^< 1 MB (Actual: ~220 KB)
echo ========================================================
echo.

cd /d "%~dp0"

set "GCC_BIN=gcc"
where gcc >nul 2>&1
if %ERRORLEVEL% neq 0 (
    if exist "C:\msys64\mingw64\bin\gcc.exe" (
        set "PATH=C:\msys64\mingw64\bin;%PATH%"
        set "GCC_BIN=C:\msys64\mingw64\bin\gcc.exe"
    ) else (
        echo [ERROR] MinGW GCC compiler not found.
        if "%~1"=="/nopause" goto :eof
        pause
        exit /b 1
    )
)

if not exist "dist" mkdir "dist"

echo [1/2] Compiling Windows resources (icon and version info)...
windres src_c\res.rc -O coff -o src_c\res.o
if %ERRORLEVEL% neq 0 (
    echo [WARNING] Resource compilation failed, continuing without resources...
    set "RES_OBJ="
) else (
    set "RES_OBJ=src_c\res.o"
)

echo [2/2] Compiling ultra-compact C binary (O3, LTO, GC-Sections, Static)...
gcc -O3 -s -flto -ffunction-sections -fdata-sections -Wl,--gc-sections -static -mwindows -municode src_c\main.c src_c\gb_ui.c src_c\pdf_engine.c %RES_OBJ% -o dist\SlideNoteFormatter.exe -lz -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32 -luser32

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Build failed! (Error code: %ERRORLEVEL%)
    echo.
    if "%~1"=="/nopause" goto :eof
    pause
    exit /b 1
)

echo.
echo ========================================================
echo   BUILD COMPLETED SUCCESSFULLY!
echo   Output: dist\SlideNoteFormatter.exe
for %%I in ("dist\SlideNoteFormatter.exe") do (
    set /a size_kb=%%~zI/1024
    echo   Size: %%~zI bytes (~!size_kb! KB / 0.2 MB)
)
echo ========================================================
echo.
if "%~1"=="/nopause" goto :eof
pause
