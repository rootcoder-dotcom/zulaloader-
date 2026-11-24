@echo off
rem YUKSEK YETKILI: loader.exe dogrulamasini ciktiyla calistirir.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0verify_loader_inner.ps1" > "%~dp0loader_verify_report.txt" 2>&1
exit /b 0
