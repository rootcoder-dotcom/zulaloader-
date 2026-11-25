@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul || goto err
set WDK=C:\Program Files (x86)\Windows Kits\10
set WVER=10.0.22621.0
set OUT=build
if not exist %OUT% mkdir %OUT%

rem surucu ayri akista: build_driver.bat -> sign.bat -> load_driver.bat
echo [1/2] loader
cl /nologo /std:c++17 /EHsc /O2 /MT /DNDEBUG /I src ^
 src\loader.cpp /Fo:%OUT%\ /Fe:%OUT%\loader.exe /link /SUBSYSTEM:CONSOLE advapi32.lib user32.lib ole32.lib shell32.lib || goto err

echo [2/2] dll
cl /nologo /std:c++17 /EHsc /O2 /MT /LD /DNDEBUG ^
 /I deps\imgui /I deps\imgui\backends /I deps\minhook\include /I src ^
 src\cheat.cpp deps\imgui\imgui.cpp deps\imgui\imgui_draw.cpp deps\imgui\imgui_tables.cpp ^
 deps\imgui\imgui_widgets.cpp deps\imgui\backends\imgui_impl_win32.cpp ^
 deps\imgui\backends\imgui_impl_dx11.cpp deps\minhook\src\hook.c deps\minhook\src\trampoline.c ^
 deps\minhook\src\buffer.c deps\minhook\src\hde\hde64.c ^
 /Fo:%OUT%\ /Fe:%OUT%\cheat.dll /link /DLL /ENTRY:zula_entry /SUBSYSTEM:WINDOWS d3d11.lib dxgi.lib ^
 user32.lib gdi32.lib imm32.lib shell32.lib dwmapi.lib advapi32.lib || goto err

echo [OK] build\zuladrv.sys build\loader.exe build\cheat.dll
exit /b 0
:err
echo [HATA] derleme basarisiz
exit /b 1