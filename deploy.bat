@echo off
REM =============================================================================
REM  deploy.bat  –  Package CineCountdown for distribution
REM
REM  Copies both .exe files into dist\ and runs windeployqt6 on each
REM  so the folder is self-contained and works on a clean Windows machine.
REM
REM  Run AFTER build.bat has succeeded.
REM  Requires windeployqt6.exe to be on PATH (ships with Qt 6).
REM =============================================================================
setlocal

where windeployqt6 >nul 2>&1 || (
    echo [ERROR] windeployqt6 not found on PATH.
    echo         Add your Qt 6 bin\ folder to PATH.
    pause
    exit /b 1
)

REM ── Locate the built executables ─────────────────────────────────────────────
REM  CMake + Ninja puts them under build\MainApp\ and build\TrayApp\
set MAIN_EXE=build\MainApp\CineCountdown.exe
set TRAY_EXE=build\TrayApp\CineCountdownTray.exe

if not exist "%MAIN_EXE%" (
    echo [ERROR] %MAIN_EXE% not found.  Run build.bat first.
    pause & exit /b 1
)
if not exist "%TRAY_EXE%" (
    echo [ERROR] %TRAY_EXE% not found.  Run build.bat first.
    pause & exit /b 1
)

REM ── Create dist folder ───────────────────────────────────────────────────────
if exist dist rmdir /s /q dist
mkdir dist

echo Copying executables...
copy /y "%MAIN_EXE%" dist\
copy /y "%TRAY_EXE%" dist\

echo Running windeployqt6...
windeployqt6 --release --no-translations dist\CineCountdown.exe
windeployqt6 --release --no-translations dist\CineCountdownTray.exe

echo.
echo ============================================================
echo  Package ready in:  dist\
echo.
echo  To run on a clean PC:
echo    1. Copy the entire dist\ folder to the target machine.
echo    2. Launch CineCountdownTray.exe first (goes to system tray).
echo    3. Launch CineCountdown.exe to manage your tiles.
echo.
echo  To auto-start the tray app on Windows login, create a
echo  shortcut to CineCountdownTray.exe and place it in:
echo    %%APPDATA%%\Microsoft\Windows\Start Menu\Programs\Startup\
echo ============================================================
echo.
pause
