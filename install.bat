@echo off
sc stop zuladrv >nul 2>&1
sc delete zuladrv >nul 2>&1
copy /y build\zuladrv.sys C:\zula\zuladrv.sys >nul
copy /y build\cheat.dll  C:\zula\cheat.dll  >nul
copy /y build\loader.exe C:\zula\loader.exe >nul
echo [OK] kuruldu, loader.exe calistir