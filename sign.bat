@echo off
setlocal
rem WDK araclari PATH'te yoksa ekle (signtool)
where signtool >nul 2>&1 || set "PATH=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64;%PATH%"
if not exist certs mkdir certs

rem [1/2] sertifika
rem   ESKI: makecert (yeni WDK'larda yok; 1024-bit RSA + SHA1)
rem   YENI: make_cert.ps1 -> New-SelfSignedCertificate (SHA256 + 2048-bit)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make_cert.ps1"
if errorlevel 1 goto err

rem [2/2] imzala
rem   ESKI: /sha1 143F7D92...  (sabit parmak izi - sertifika yeniden uretilince patliyordu)
rem   YENI: /n ZulaDev         (konuya gore secim)
signtool sign /v /a /n ZulaDev /s PrivateCertStore /fd sha256 /tr http://timestamp.digicert.com /td sha256 build\zuladrv.sys
if errorlevel 1 (
  echo [!] /n ZulaDev ile secilemedi, /a ile tekrar deneniyor...
  signtool sign /v /a /s PrivateCertStore /fd sha256 /tr http://timestamp.digicert.com /td sha256 build\zuladrv.sys
  if errorlevel 1 goto err
)

echo [OK] imzalandi
exit /b 0
:err
echo [HATA] imzalama basarisiz
exit /b 1
