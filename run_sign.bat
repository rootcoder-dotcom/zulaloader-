@echo off
cd /d "%~dp0"
call sign.bat > sign_log.txt 2>&1
echo SIGN_EXIT=%ERRORLEVEL% >> sign_log.txt
exit /b 0
