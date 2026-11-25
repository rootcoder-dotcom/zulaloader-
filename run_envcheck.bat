@echo off
(
echo === ELEVATED ORTAM ===
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Baran\Desktop\zula\envcheck.ps1"
echo === ELEVATED CIHAZ KONTROL ===
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Baran\Desktop\zula\checkdev.ps1"
) > "C:\Users\Baran\Desktop\zula\env_out.txt" 2>&1
exit /b 0
