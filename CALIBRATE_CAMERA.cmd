@echo off
call "%~dp0server\opencv\calibration\RunCameraCalibration.cmd" %*
exit /b %ERRORLEVEL%
