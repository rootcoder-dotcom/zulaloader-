@echo off
rem YUKSEK YETKILI: 577 hatasi tehsisi -> diagnose_ci_log.txt
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0diagnose_ci.ps1" > "%~dp0diagnose_ci_log.txt" 2>&1
exit /b 0
