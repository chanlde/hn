@echo off
setlocal
cd /d "%~dp0"

set APP_NAME=SolarCleanFirmwareUpdater
set FINAL_EXE=%CD%\%APP_NAME%.exe
set DIST_TMP=%CD%\_dist_tmp
set BUILD_TMP=%CD%\_build_tmp

if exist "%FINAL_EXE%" del /f /q "%FINAL_EXE%"
if exist "%APP_NAME%.zip" del /f /q "%APP_NAME%.zip"
if exist "%DIST_TMP%" rmdir /s /q "%DIST_TMP%"
if exist "%BUILD_TMP%" rmdir /s /q "%BUILD_TMP%"

python -m pip show pyinstaller >nul 2>nul
if errorlevel 1 (
    echo Installing pyinstaller...
    python -m pip install pyinstaller
)

python -m PyInstaller --noconfirm --clean --windowed --onefile --noupx ^
  --name %APP_NAME% ^
  --hidden-import serial.tools.list_ports ^
  --hidden-import serial.tools.list_ports_windows ^
  --hidden-import PyQt5.sip ^
  --runtime-hook runtime_hook.py ^
  --distpath "%DIST_TMP%" ^
  --workpath "%BUILD_TMP%" ^
  main.py

if errorlevel 1 goto failed

move /y "%DIST_TMP%\%APP_NAME%.exe" "%FINAL_EXE%" >nul

if exist "%DIST_TMP%" rmdir /s /q "%DIST_TMP%"
if exist "%BUILD_TMP%" rmdir /s /q "%BUILD_TMP%"
if exist "%APP_NAME%.spec" del /f /q "%APP_NAME%.spec"

echo.
echo Build output: %FINAL_EXE%
echo Deliver this exe file only.
endlocal
exit /b 0

:failed
echo.
echo Build failed.
endlocal
exit /b 1
