@echo off
rem deploy.bat'i yonlendirilmis ciktiyla calistirir (yuksek yetkili pencere ciktisi kaybolmasin)
call "%~dp0deploy.bat" > "%~dp0deploy_log.txt" 2>&1
exit /b %errorlevel%
