@echo off
setlocal
rem zuladrv servis onarimi: stop -> delete -> kayit yok olana kadar bekle
rem -> C:\zula yolundan yeniden olustur + start. Cikti service_fix_log.txt.
set LOG=%~dp0service_fix_log.txt
> "%LOG%" echo === zuladrv servis onarimi %date% %time% ===

>> "%LOG%" echo.
>> "%LOG%" echo [1/5] oncesi durum:
sc query zuladrv >> "%LOG%" 2>&1
sc qc zuladrv >> "%LOG%" 2>&1

>> "%LOG%" echo.
>> "%LOG%" echo [2/5] stop deneniyor...
sc stop zuladrv >> "%LOG%" 2>&1
>> "%LOG%" echo stop hata kodu: %errorlevel%

>> "%LOG%" echo.
>> "%LOG%" echo [3/5] RUNNING biter mi bekleniyor (30 sn)...
set /a N=0
:waitstop
sc query zuladrv 2>&1 | findstr /c:"STATE" | findstr /c:"RUNNING" >nul
if errorlevel 1 goto stopped
set /a N+=1
if %N% geq 30 goto stopped
timeout /t 1 /nobreak >nul
goto waitstop
:stopped
>> "%LOG%" echo durum kontrol edildi (N=%N%)

>> "%LOG%" echo.
>> "%LOG%" echo [4/5] delete + kayit silinmesi bekleniyor (45 sn)...
sc delete zuladrv >> "%LOG%" 2>&1
>> "%LOG%" echo delete hata kodu: %errorlevel%
set /a N=0
:waitgone
sc query zuladrv 2>&1 | findstr /c:"SERVICE_NAME" >nul
if errorlevel 1 goto gone
set /a N+=1
if %N% geq 45 goto gonefail
timeout /t 1 /nobreak >nul
goto waitgone
:gone
>> "%LOG%" echo [OK] servis kaydi silindi (N=%N%)
goto recreate

:gonefail
>> "%LOG%" echo [HATA] servis kaydi hala var - acik handle tutuyor olabilir
sc query zuladrv >> "%LOG%" 2>&1
goto end

:recreate
>> "%LOG%" echo.
>> "%LOG%" echo [5/5] C:\zula yolundan yeniden olusturuluyor...
sc create zuladrv type= kernel start= demand binPath= "\??\C:\zula\zuladrv.sys" DisplayName= "zuladrv" >> "%LOG%" 2>&1
if errorlevel 1 (
  >> "%LOG%" echo [HATA] sc create hata: %errorlevel%
  goto end
)
sc start zuladrv >> "%LOG%" 2>&1
if errorlevel 1 (
  >> "%LOG%" echo [HATA] sc start hata: %errorlevel%
  sc query zuladrv >> "%LOG%" 2>&1
  goto end
)
>> "%LOG%" echo [OK] servis C:\zula yolundan calisiyor
sc query zuladrv >> "%LOG%" 2>&1
sc qc zuladrv >> "%LOG%" 2>&1

:end
>> "%LOG%" echo === bitti ===
exit /b 0
