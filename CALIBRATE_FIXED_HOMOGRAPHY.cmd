@echo off
call "%~dp0server\opencv\calibration\RunFixedHomographyCalibration.cmd" %*
exit /b %ERRORLEVEL%
