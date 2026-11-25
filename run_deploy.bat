@echo off
setlocal
set ROOT=C:\Users\Baran\Desktop\zula
set LOG=%ROOT%\deploy_log.txt
> "%LOG%" echo ===== deploy basladi %DATE% %TIME%

cd /d "%ROOT%"
call sign.bat >> "%LOG%" 2>&1
>> "%LOG%" echo SIGN_EXIT=%ERRORLEVEL%

copy /y "%ROOT%\build\zuladrv.sys" "C:\zula\zuladrv.sys" >> "%LOG%" 2>&1
>> "%LOG%" echo COPY_EXIT=%ERRORLEVEL%

cd /d "%ROOT%\deps\kdmapper\kdmapper"
kdmapper.exe "C:\zula\zuladrv.sys" < nul >> "%LOG%" 2>&1
>> "%LOG%" echo KDMAPPER_EXIT=%ERRORLEVEL%

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\checkdev.ps1" >> "%LOG%" 2>&1

>> "%LOG%" echo ===== deploy bitti
exit /b 0
