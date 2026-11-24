@echo off
setlocal
set LOG=%~dp0svc_diag2_log.txt
> "%LOG%" echo === svc diag2 %date% %time% ===

>> "%LOG%" echo.
>> "%LOG%" echo [1] registry Start degeri:
reg query "HKLM\SYSTEM\CurrentControlSet\Services\zuladrv" /v Start >> "%LOG%" 2>&1

>> "%LOG%" echo.
>> "%LOG%" echo [2] registry genel anahtar listesi:
reg query "HKLM\SYSTEM\CurrentControlSet\Services\zuladrv" >> "%LOG%" 2>&1

>> "%LOG%" echo.
>> "%LOG%" echo [3] sc queryex:
sc queryex zuladrv >> "%LOG%" 2>&1

>> "%LOG%" echo.
>> "%LOG%" echo [4] ornek driver servisleri (karsilastirma):
sc query Tcpip >> "%LOG%" 2>&1
sc query Ntfs >> "%LOG%" 2>&1

>> "%LOG%" echo.
>> "%LOG%" echo [5] stop tekrar deneniyor:
sc stop zuladrv >> "%LOG%" 2>&1
>> "%LOG%" echo stop hata kodu: %errorlevel%

>> "%LOG%" echo.
>> "%LOG%" echo [6] net stop denemesi:
net stop zuladrv >> "%LOG%" 2>&1
>> "%LOG%" echo net stop hata kodu: %errorlevel%

>> "%LOG%" echo.
>> "%LOG%" echo [7] son durum:
sc query zuladrv >> "%LOG%" 2>&1
sc qc zuladrv >> "%LOG%" 2>&1

>> "%LOG%" echo === bitti ===
exit /b 0
