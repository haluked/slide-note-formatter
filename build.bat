@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul
echo ========================================================
echo   SlideNoteFormatter - Ultra-Lightweight C/Win32 Build
echo   Target Size: ^< 1 MB (Actual: ~210 KB)
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
        echo [HATA] MinGW GCC derleyicisi bulunamadi.
        echo Lutfen MinGW-w64 kurulu oldugundan emin olun.
        pause
        exit /b 1
    )
)

if not exist "dist" mkdir "dist"

echo [1/2] Windows kaynaklari derleniyor (Icon ve Version info)...
windres src_c\res.rc -O coff -o src_c\res.o
if %ERRORLEVEL% neq 0 (
    echo [UYARI] Kaynak derleme hatasi, kaynaksiz devam ediliyor...
    set "RES_OBJ="
) else (
    set "RES_OBJ=src_c\res.o"
)

echo [2/2] Ultra-kucuk C ikili dosyasi derleniyor (O3, LTO, GC-Sections, Static)...
gcc -O3 -s -flto -ffunction-sections -fdata-sections -Wl,--gc-sections -static -mwindows -municode src_c\main.c src_c\pdf_engine.c %RES_OBJ% -o dist\SlideNoteFormatter.exe -lz -lcomctl32 -lcomdlg32 -lshell32 -lole32 -lgdi32 -luser32

if %ERRORLEVEL% neq 0 (
    echo.
    echo [HATA] Derleme basarisiz oldu! (Hata kodu: %ERRORLEVEL%)
    echo.
    pause
    exit /b 1
)

echo.
echo ========================================================
echo   DERLEME BASARIYLA TAMAMLANDI!
echo   Cikti: dist\SlideNoteFormatter.exe
for %%I in ("dist\SlideNoteFormatter.exe") do (
    set /a size_kb=%%~zI/1024
    echo   Boyut: %%~zI bayt (~!size_kb! KB / 0.2 MB)
)
echo ========================================================
echo.
pause
