@echo off
rem YUKSEK YETKILI: bcdedit ayarlari + HVCI kapatma -> bcd_apply_log.txt (reboot YOK)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bcd_apply.ps1" > "%~dp0bcd_apply_log.txt" 2>&1
exit /b 0
