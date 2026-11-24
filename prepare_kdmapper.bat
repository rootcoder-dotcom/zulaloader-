@echo off
rem YUKSEK YETKILI: Defender exclusion + blocklist kapat + servis sil + imzala
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare_kdmapper.ps1" > "%~dp0prepare_kdmapper_log.txt" 2>&1
exit /b 0
