@echo off
call "%~dp0server\opencv\calibration\ConfigureArucoChannel.cmd" %*
exit /b %ERRORLEVEL%
