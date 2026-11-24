@echo off
setlocal
pushd "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul 2>&1
if errorlevel 1 (
  echo [HATA] vcvars64 basarisiz
  exit /b 1
)
if not exist build mkdir build

echo [1/3] testdep.dll (map hedefi)...
cl /nologo /LD /MD /O2 /DNDEBUG /Fo:build\testdep.obj /Fe:build\testdep.dll ^
  /IMPLIB:build\testdep.lib src\testdep.cpp /link /nologo /SUBSYSTEM:CONSOLE
if errorlevel 1 (
  echo [HATA] testdep derleme
  exit /b 1
)

echo [2/3] test_driver.exe...
cl /nologo /O2 /MT /DNDEBUG /I src /Fo:build\test_driver.obj /Fe:build\test_driver.exe ^
  src\test_driver.cpp /link /nologo /SUBSYSTEM:CONSOLE
if errorlevel 1 (
  echo [HATA] test_driver derleme
  exit /b 1
)

echo [3/3] test calisiyor...
build\test_driver.exe
exit /b %errorlevel%
