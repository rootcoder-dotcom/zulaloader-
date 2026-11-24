@echo off
setlocal
rem Kendini log dosyasina yeniden cagir; cikti load_log.txt'e yazilsin
if not "%~1"=="log" (
  >"%~dp0load_log.txt" echo load_driver basladi: %date% %time%
  "%~f0" log >> "%~dp0load_log.txt" 2>&1
  exit /b %errorlevel%
)

set SYS=%~dp0build\zuladrv.sys
set SVC=zuladrv
set CER=%~dp0certs\zula.cer

echo ============================================
echo  load_driver - %date% %time%
echo ============================================

if not exist "%SYS%" (
  echo [HATA] %SYS% bulunamadi - once build_and_sign.bat calistirin
  exit /b 1
)

echo.
echo [1/4] Sertifika machine store'a kuruluyor...
if exist "%CER%" (
  certutil -addstore -f Root "%CER%"
  certutil -addstore -f TrustedPublisher "%CER%"
) else (
  echo [UYARI] %CER% yok, atlandi
)

echo.
echo [2/4] Servis guncelleniyor (%SVC%)...
sc stop %SVC% >nul 2>&1
sc delete %SVC% >nul 2>&1
sc create %SVC% type= kernel start= demand binPath= "\??\%SYS%" DisplayName= "zuladrv"
if errorlevel 1 (
  echo [HATA] sc create basarisiz
  exit /b 1
)

echo.
echo [3/4] Driver baslatiliyor...
sc start %SVC%
if not errorlevel 1 goto loaded

echo.
echo [4/4] Baslatma basarisiz - imza denetimi engeli olabilir:
sc query %SVC%
echo.
echo Test signing deneniyor...
bcdedit /set testsigning on
if errorlevel 1 (
  echo [HATA] bcdedit basarisiz - Secure Boot acik olabilir
  echo Alternatif: install_driver.bat'i yonetici olarak calistirin
  exit /b 1
)
echo [OK] testsigning ACILDI
echo [!] YENIDEN BASLATMA GEREKIYOR - restart sonrasi bu scripti tekrar calistirin
exit /b 2

:loaded
echo.
echo ============================================
echo  [OK] DRIVER KERNEL SEVIYESINDE YUKLENDI
echo ============================================
sc query %SVC%
echo.
echo Cihaz: \\.\zuladrv  (Device: \Device\zuladrv)
exit /b 0
