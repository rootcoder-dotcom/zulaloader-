@echo off
setlocal
rem repair3: kayit durumunu duzelt (DeleteFlag/Start) -> cihaz kontrolu ->
rem NtUnloadDriver -> kayit sil -> C:\zula yolundan yeniden kur -> stop/start testi.
rem Cikti repair3_log.txt'ye yazilir.
set LOG=%~dp0repair3_log.txt
> "%LOG%" echo === repair3 %date% %time% ===

>> "%LOG%" echo.
>> "%LOG%" echo [1] kayit durumu duzeltiliyor (DeleteFlag siliniyor, Start= demand)...
reg delete "HKLM\SYSTEM\CurrentControlSet\Services\zuladrv" /v DeleteFlag /f >> "%LOG%" 2>&1
>> "%LOG%" echo DeleteFlag silme hata kodu: %errorlevel%
sc config zuladrv start= demand >> "%LOG%" 2>&1
>> "%LOG%" echo sc config hata kodu: %errorlevel%
sc qc zuladrv >> "%LOG%" 2>&1

>> "%LOG%" echo.
>> "%LOG%" echo [2] cihaz baglanti testi...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0probe_device.ps1" >> "%LOG%" 2>&1

>> "%LOG%" echo.
>> "%LOG%" echo [3] NtUnloadDriver deneniyor...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0unload_driver.ps1" >> "%LOG%" 2>&1
timeout /t 2 /nobreak >nul
>> "%LOG%" echo.
>> "%LOG%" echo [3b] unload sonrasi durum:
sc query zuladrv >> "%LOG%" 2>&1

>> "%LOG%" echo.
>> "%LOG%" echo [4] servis kaydi silinene kadar bekleniyor (30 sn)...
set /a N=0
:waitgone
sc query zuladrv 2>&1 | findstr /c:"SERVICE_NAME" >nul
if errorlevel 1 goto gone
set /a N+=1
if %N% geq 30 goto stillhere
timeout /t 1 /nobreak >nul
goto waitgone

:gone
>> "%LOG%" echo [OK] servis kaydi silindi (N=%N% sn)
goto create

:stillhere
>> "%LOG%" echo [UYARI] kayit hala var - sc stop + sc delete deneniyor...
sc stop zuladrv >> "%LOG%" 2>&1
>> "%LOG%" echo stop hata kodu: %errorlevel%
sc delete zuladrv >> "%LOG%" 2>&1
>> "%LOG%" echo delete hata kodu: %errorlevel%
set /a N=0
:waitgone2
sc query zuladrv 2>&1 | findstr /c:"SERVICE_NAME" >nul
if errorlevel 1 goto create
set /a N+=1
if %N% geq 30 goto fail
timeout /t 1 /nobreak >nul
goto waitgone2

:create
>> "%LOG%" echo.
>> "%LOG%" echo [5] C:\zula yolundan servis olusturuluyor...
sc create zuladrv type= kernel start= demand binPath= "\??\C:\zula\zuladrv.sys" DisplayName= "zuladrv" >> "%LOG%" 2>&1
if errorlevel 1 (
  >> "%LOG%" echo [HATA] sc create hata: %errorlevel%
  goto end
)
sc start zuladrv >> "%LOG%" 2>&1
>> "%LOG%" echo start hata kodu: %errorlevel%
timeout /t 2 /nobreak >nul
sc query zuladrv >> "%LOG%" 2>&1
sc qc zuladrv >> "%LOG%" 2>&1

>> "%LOG%" echo.
>> "%LOG%" echo [6] stop/start dongusu ile SCM kontrolu test ediliyor...
sc stop zuladrv >> "%LOG%" 2>&1
>> "%LOG%" echo stop hata kodu: %errorlevel%
timeout /t 2 /nobreak >nul
sc start zuladrv >> "%LOG%" 2>&1
>> "%LOG%" echo start2 hata kodu: %errorlevel%
timeout /t 1 /nobreak >nul
sc query zuladrv >> "%LOG%" 2>&1
goto end

:fail
>> "%LOG%" echo [HATA] kayit silinemedi - yeniden baslatma (reboot) gerekebilir
sc query zuladrv >> "%LOG%" 2>&1

:end
>> "%LOG%" echo === bitti ===
exit /b 0
