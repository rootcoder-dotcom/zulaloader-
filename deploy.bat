@echo off
rem Reboot sonrasi: imzala + C:\zula'ya kopyala + kdmapper + cihaz kontrolu
rem Tek UAC yeterlidir.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy.ps1"
