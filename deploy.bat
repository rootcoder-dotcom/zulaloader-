@echo off
setlocal
pushd "%~dp0"
rem C:\zula'ya deploy + servisi C:\zula\zuladrv.sys yolundan yeniden kur
rem (loader.exe install() ile ayni akis) - admin gerektirir

echo ============================================
echo  deploy - %date% %time%
echo ============================================

echo.
echo [1/3] Dosyalar C:\zula'ya kopyalaniyor...
call install.bat
if not exist C:\zula\zuladrv.sys (
  echo [HATA] C:\zula\zuladrv.sys kopyalanamadi
  exit /b 1
)
if not exist C:\zula\cheat.dll (
  echo [HATA] C:\zula\cheat.dll kopyalanamadi
  exit /b 1
)
if not exist C:\zula\loader.exe (
  echo [HATA] C:\zula\loader.exe kopyalanamadi
  exit /b 1
)
echo [OK] 3 dosya kopyalandi

echo.
echo [2/3] Servis C:\zula yolundan yeniden olusturuluyor...
sc stop zuladrv >nul 2>&1
sc delete zuladrv >nul 2>&1

rem Kayit silinene kadar bekle (1072 MARKED_FOR_DELETE tuzağına dusmemek icin)
set /a N=0
:waitdel
sc query zuladrv >nul 2>&1
if errorlevel 1 goto deleted
set /a N+=1
if %N% geq 20 goto deleted
timeout /t 1 /nobreak >nul
goto waitdel
:deleted
echo     kayit silindi/beklendi (N=%N%)

sc create zuladrv type= kernel start= demand binPath= "\??\C:\zula\zuladrv.sys" DisplayName= "zuladrv"
if errorlevel 1 (
  echo [HATA] sc create
  exit /b 1
)

echo.
echo [3/3] Driver baslatiliyor...
sc start zuladrv
if errorlevel 1 (
  echo [HATA] sc start
  sc query zuladrv
  exit /b 1
)

echo.
echo ============================================
echo  [OK] DEPLOY TAMAMLANDI
echo  Dosyalar : C:\zula\ (zuladrv.sys, cheat.dll, loader.exe)
echo  Servis   : C:\zula\zuladrv.sys -> RUNNING
echo ============================================
sc query zuladrv
exit /b 0
