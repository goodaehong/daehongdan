@echo off
setlocal

set "camera_channel=%~1"
if "%camera_channel%"=="" set /p "camera_channel=Camera channel (1-4): "

echo %camera_channel%| findstr /r /x "[1-4]" >nul
if errorlevel 1 (
    echo Invalid channel. Enter 1, 2, 3, or 4.
    pause
    exit /b 2
)

rem Bypass the PowerShell execution policy for this process only.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0RunCameraCalibration.ps1" -Channel %camera_channel%
set "exit_code=%ERRORLEVEL%"

if not "%exit_code%"=="0" (
    echo.
    echo Calibration failed with exit code %exit_code%.
    pause
    exit /b %exit_code%
)

echo.
echo Calibration tool finished.
pause
exit /b 0
