# Zula build script
Write-Host "Zula build script v1.0" -ForegroundColor Cyan
Write-Host "======================" -ForegroundColor Cyan

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

# VS2022 yolu
$VSPath = "C:\Program Files\Microsoft Visual Studio\2022\Community"
if (-not (Test-Path "$VSPath\Common7\Tools\VsDevCmd.bat")) {
    $VSPath = "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community"
    if (-not (Test-Path "$VSPath\Common7\Tools\VsDevCmd.bat")) {
        Write-Host "HATA: Visual Studio 2022 bulunamadi" -ForegroundColor Red
        exit 1
    }
}

Write-Host "Using: $VSPath" -ForegroundColor Green

# VS environment'i yukle
$env:VSDEVCMD_ARGS = "-arch=amd64"
& "$VSPath\Common7\Tools\VsDevCmd.bat"

# Deps kontrolu
if (-not (Test-Path "deps\imgui")) {
    Write-Host "HATA: deps\imgui eksik" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path "deps\minhook")) {
    Write-Host "HATA: deps\minhook eksik" -ForegroundColor Red
    exit 1
}

# Build klasoru
if (-not (Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
}

Write-Host ""
Write-Host "Compiling cheat.cpp..." -ForegroundColor Yellow

# cheat.cpp compile
cl.exe /nologo /Zi /MT /EHsc /W4 /WX /O2 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DAMD64 /D_WIN64 /I. /Ideps\imgui /Ideps\minhook\src /Ideps\minhook\include /c cheat.cpp /Fobuild\cheat.obj
if ($LASTEXITCODE -ne 0) {
    Write-Host "HATA: cheat.cpp derleme hatasi" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Compiling loader.cpp..." -ForegroundColor Yellow

# loader.cpp compile
cl.exe /nologo /Zi /MT /EHsc /W4 /WX /O2 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DAMD64 /D_WIN64 /I. /Ideps\imgui /Ideps\minhook\src /Ideps\minhook\include /c loader.cpp /Fobuild\loader.obj
if ($LASTEXITCODE -ne 0) {
    Write-Host "HATA: loader.cpp derleme hatasi" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Linking cheat.dll..." -ForegroundColor Yellow

# Link DLL
link.exe /nologo /DEBUG /DLL /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /OUT:build\cheat.dll build\cheat.obj deps\imgui\backends\imgui_impl_dx11.cpp deps\imgui\backends\imgui_impl_win32.cpp deps\imgui\imgui*.cpp deps\minhook\src\*.c d3d11.lib dxgi.lib user32.lib gdi32.lib advapi32.lib
if ($LASTEXITCODE -ne 0) {
    Write-Host "HATA: cheat.dll linking hatasi" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Linking loader.exe..." -ForegroundColor Yellow

# Link loader
link.exe /nologo /DEBUG /SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF /OUT:build\loader.exe build\loader.obj ntdll.lib advapi32.lib
if ($LASTEXITCODE -ne 0) {
    Write-Host "HATA: loader.exe linking hatasi" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Derleme TAMAMLANDI!" -ForegroundColor Green
Write-Host "==================" -ForegroundColor Green
Get-ChildItem build\