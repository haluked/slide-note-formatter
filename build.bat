@echo off
chcp 65001 >nul
echo ========================================================
echo     SlideNoteFormatter - Standalone EXE Derleyici
echo ========================================================
echo.

cd /d "%~dp0"

echo [1/3] Python sanal ortam / bagimlilik kontrol ediliyor...
python -m pip install -q -r requirements.txt

echo [2/3] PyInstaller ile tek dosya .EXE derleniyor...
python -m PyInstaller ^
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

echo.
echo [3/3] Derleme tamamlandi!
echo Cikti: dist\SlideNoteFormatter.exe
echo.
pause
