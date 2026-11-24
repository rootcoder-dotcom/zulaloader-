@echo off
setlocal
rem WDK araclari PATH'te yoksa ekle (signtool)
where signtool >nul 2>&1 || set "PATH=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64;%PATH%"
if not exist certs mkdir certs

rem [1/3] sertifika - LocalMachine\My + machine Root/TrustedPublisher
rem   (user store'a kurulan zincir SCM tarafinda gorulmuyordu -> 577)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make_cert.ps1"
if errorlevel 1 goto err

rem [2/3] imzala - machine store (/sm), sertifika adi ile secim (/n ZulaDev)
rem   ESKI: /s PrivateCertStore (user store) / sabit /sha1
rem   YENI: /sm /s My           (LocalMachine\My)
signtool sign /v /a /sm /n ZulaDev /s My /fd sha256 /tr http://timestamp.digicert.com /td sha256 build\zuladrv.sys
if errorlevel 1 goto err

rem [3/3] dogrula - verify /pa (default policy .sys'te farkli davranir)
signtool verify /pa /v build\zuladrv.sys
if errorlevel 1 goto err

echo [OK] imzalandi ve dogrulandi
exit /b 0
:err
echo [HATA] imzalama/dogrulama basarisiz
exit /b 1
