@echo off
setlocal
chcp 65001 >nul
echo ========================================================
echo     SlideNoteFormatter - Standalone EXE Derleyici
echo ========================================================
echo.

cd /d "%~dp0"

set "PYTHON_EXE="

:: 1. Standard PATH python
python -c "import sys" >nul 2>&1
if %ERRORLEVEL% equ 0 (
    set "PYTHON_EXE=python"
    goto :found_python
)

:: 2. Python launcher (py -3)
py -3 -c "import sys" >nul 2>&1
if %ERRORLEVEL% equ 0 (
    set "PYTHON_EXE=py -3"
    goto :found_python
)

:: 3. WindowsApps PythonSoftwareFoundation package
for /f "delims=" %%I in ('dir /b /ad "%LOCALAPPDATA%\Microsoft\WindowsApps\PythonSoftwareFoundation*" 2^>nul') do (
    if exist "%LOCALAPPDATA%\Microsoft\WindowsApps\%%I\python.exe" (
        set "PYTHON_EXE=%LOCALAPPDATA%\Microsoft\WindowsApps\%%I\python.exe"
        goto :found_python
    )
)

:: 4. User Programs Python
for /d %%I in ("%LOCALAPPDATA%\Programs\Python\Python3*") do (
    if exist "%%I\python.exe" (
        set "PYTHON_EXE=%%I\python.exe"
        goto :found_python
    )
)

:: 5. System Program Files Python
for /d %%I in ("C:\Program Files\Python3*") do (
    if exist "%%I\python.exe" (
        set "PYTHON_EXE=%%I\python.exe"
        goto :found_python
    )
)

echo [HATA] Sisteminizde calisir bir Python 3 ortami bulunamadi.
echo Lutfen Python 3.10+ kurulu oldugundan emin olun.
echo.
pause
exit /b 1

:found_python
echo [1/3] Python tespit edildi: %PYTHON_EXE%
echo [1/3] Bagimliliklar kontrol ediliyor...
"%PYTHON_EXE%" -m pip install -q -r requirements.txt 2>nul
if %ERRORLEVEL% neq 0 (
    %PYTHON_EXE% -m pip install -q -r requirements.txt
)

echo.
echo [2/3] PyInstaller ile tek dosya .EXE derleniyor...
"%PYTHON_EXE%" -m PyInstaller ^
    --noconsole ^
    --onefile ^
    --collect-all customtkinter ^
    --add-data "assets\icon.ico;assets" ^
    --add-data "assets\polito_logo.png;assets" ^
    --icon "assets\icon.ico" ^
    --exclude-module torch ^
    --exclude-module torchvision ^
    --exclude-module scipy ^
    --exclude-module pandas ^
    --exclude-module matplotlib ^
    --exclude-module cv2 ^
    --exclude-module onnxruntime ^
    --exclude-module sympy ^
    --name "SlideNoteFormatter" ^
    --distpath "dist" ^
    "src\app.py"

if %ERRORLEVEL% neq 0 (
    echo.
    echo [HATA] Derleme basarisiz oldu! (Hata kodu: %ERRORLEVEL%)
    echo.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo ========================================================
echo [3/3] Derleme basariyla tamamlandi!
echo Cikti: dist\SlideNoteFormatter.exe
echo ========================================================
echo.
pause
