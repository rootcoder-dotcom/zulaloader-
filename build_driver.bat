@echo off
setlocal
pushd "%~dp0"

echo [DRIVER] Surucu derleniyor...

set WDK=C:\Program Files (x86)\Windows Kits\10
set WVER=10.0.22621.0

if not exist build mkdir build

set INCLUDE=%WDK%\Include\%WVER%\km;%WDK%\Include\%WVER%\km\crt;%WDK%\Include\%WVER%\shared;%WDK%\Include\%WVER%\ucrt;%CD%\src
set LIB=%WDK%\Lib\%WVER%\km\x64

echo INCLUDE: %INCLUDE%
echo LIB: %LIB%

echo.
echo Compiling driver.cpp...
cl /nologo /c /Zi /Zl /GS- /O2 /W3 /D_AMD64_ /DAMD64 /D_WIN64 /D_KERNEL_MODE /DUNICODE /D_UNICODE ^
 /I "%WDK%\Include\%WVER%\km" /I "%WDK%\Include\%WVER%\km\crt" /I "%WDK%\Include\%WVER%\shared" /I "%WDK%\Include\%WVER%\ucrt" /I src ^
 /Fo:build\driver.obj src\driver.cpp

if errorlevel 1 (
    echo [HATA] driver compile
    pause
    popd
    exit /b 1
)

echo.
echo Linking driver...
link /nologo /DRIVER:WDM /SUBSYSTEM:NATIVE /ENTRY:DriverEntry /OUT:build\zuladrv.sys ^
 /LIBPATH:"%WDK%\Lib\%WVER%\km\x64" ^
 build\driver.obj ntoskrnl.lib hal.lib wdmsec.lib

if errorlevel 1 (
    echo [HATA] driver link
    pause
    popd
    exit /b 1
)

echo.
echo [OK] build\zuladrv.sys olustu
popd
pause