@echo off
cd /d C:\Users\Baran\Desktop\zula
call sign.bat > sign_log.txt 2>&1
exit /b %errorlevel%
