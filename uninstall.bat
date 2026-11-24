@echo off
sc stop zuladrv >nul 2>&1
sc delete zuladrv >nul 2>&1
echo [OK] kaldirildi