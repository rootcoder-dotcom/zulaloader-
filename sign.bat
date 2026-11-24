@echo off
setlocal
rem WDK araclari PATH'te yoksa ekle (signtool/makecert)
where signtool >nul 2>&1 || set "PATH=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64;%PATH%"
where makecert >nul 2>&1 || set "PATH=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64;%PATH%"
if not exist certs mkdir certs
if not exist certs\zula.cer (
  makecert -r -pe -ss PrivateCertStore -n "CN=ZulaDev" certs\zula.cer || goto err
)
rem machine store'lar admin ister; yetki yoksa kullanici store'una kur
certutil -addstore -f Root certs\zula.cer >nul 2>&1 || certutil -user -addstore -f Root certs\zula.cer >nul 2>&1
certutil -addstore -f TrustedPublisher certs\zula.cer >nul 2>&1 || certutil -user -addstore -f TrustedPublisher certs\zula.cer >nul 2>&1
rem /sha1: machine store'a kurulan sertifika (kernel imza zinciri ona bagli)
signtool sign /v /sha1 143F7D92A9BE34CBD95AD683811FFDBF039BB5B4 /s PrivateCertStore /fd sha256 /tr http://timestamp.digicert.com /td sha256 build\zuladrv.sys || goto err
echo [OK] imzalandi
exit /b 0
:err
echo [HATA] imzalama basarisiz
exit /b 1