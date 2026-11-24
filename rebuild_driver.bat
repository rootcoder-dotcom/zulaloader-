@echo off
setlocal
pushd "%~dp0"
rem Surucu degisti: durdur (dosya kilidini birak) -> derle -> imzala -> yukle
rem Tamami admin gerektirir; elevated calistirilir

echo ============================================
echo  rebuild_driver - %date% %time%
echo ============================================

echo.
echo [1/4] Surucu durduruluyor...
sc stop zuladrv >nul 2>&1
sc delete zuladrv >nul 2>&1
timeout /t 1 /nobreak >nul

echo.
echo [2/4] Surucu derleniyor...
call build_driver.bat < nul
if errorlevel 1 (
  echo [HATA] driver build
  exit /b 1
)

echo.
echo [3/4] Surucu imzalaniyor...
call sign.bat
if errorlevel 1 (
  echo [HATA] sign
  exit /b 1
)

echo.
echo [4/4] Surucu yukleniyor...
call load_driver.bat
if errorlevel 1 (
  echo [HATA] load
  exit /b 1
)

echo.
echo ============================================
echo  [OK] REBUILD TAMAMLANDI
echo ============================================
exit /b 0
