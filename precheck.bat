@echo off
rem YUKSEK YETKILI: bcdedit/HVCI/SecureBoot/BitLocker on denetimi -> precheck_log.txt
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0precheck.ps1" > "%~dp0precheck_log.txt" 2>&1
exit /b 0
