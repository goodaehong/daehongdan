@echo off
call "%~dp0server\opencv\calibration\SetupArucoChannel.cmd" %*
exit /b %ERRORLEVEL%
