@echo off
cd /d "C:\Users\Baran\Desktop\zula\deps\kdmapper\kdmapper"
kdmapper.exe "C:\zula\zuladrv.sys" < nul > "C:\Users\Baran\Desktop\zula\kdmapper_log.txt" 2>&1
echo KDMAPPER_EXIT=%ERRORLEVEL% >> "C:\Users\Baran\Desktop\zula\kdmapper_log.txt"
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Baran\Desktop\zula\checkdev.ps1" >> "C:\Users\Baran\Desktop\zula\kdmapper_log.txt" 2>&1
ping -n 6 127.0.0.1 >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Baran\Desktop\zula\checkdev.ps1" >> "C:\Users\Baran\Desktop\zula\kdmapper_log.txt" 2>&1
exit /b 0
