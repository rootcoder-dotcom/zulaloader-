# bcd_apply.ps1 - bcdedit ayarlari + HVCI (Bellek butunlugu) kapatma
# REBOOT BU SCRIPT'TE YOK - kullanici onayiyla ayri yapilir.
$ErrorActionPreference = 'Continue'

function Run($c) {
    Write-Output ""
    Write-Output (">>> " + $c)
    $o = cmd /c "$c 2>&1"
    $o | ForEach-Object { Write-Output ("    " + $_) }
    Write-Output ("    exit=" + $LASTEXITCODE)
}

Write-Output "=================================================="
Write-Output ("bcd_apply - " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Write-Output "=================================================="

Write-Output ""
Write-Output "[1] mevcut durum (uygulama ONCESI)"
cmd /c "bcdedit /enum {current} 2>&1"

Write-Output ""
Write-Output "[2] bcdedit ayarlari"
Run "bcdedit /set testsigning off"
Run "bcdedit /set nointegritychecks on"
Run "bcdedit /set integrityservices disable"
Run "bcdedit /set debug off"

Write-Output ""
Write-Output "[3] HVCI / Bellek butunlugu kapatiliyor"
$k = 'HKLM:\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity'
if (-not (Test-Path $k)) { New-Item -Path $k -Force | Out-Null }
New-ItemProperty -Path $k -Name 'Enabled' -PropertyType DWord -Value 0 -Force | Out-Null
if ((Get-ItemProperty -Path $k -Name 'Locked' -ErrorAction SilentlyContinue) -ne $null) {
    New-ItemProperty -Path $k -Name 'Locked' -PropertyType DWord -Value 0 -Force | Out-Null
    Write-Output "    Locked degeri 0 yapildi"
}
Write-Output ("    HVCI Enabled = " + (Get-ItemProperty -Path $k -Name 'Enabled').Enabled)

Write-Output ""
Write-Output "[4] dogrulama (uygulama SONRASI - reboot oncesi)"
cmd /c "bcdedit /enum {current} 2>&1"

Write-Output ""
Write-Output "[5] VBS durumu (reboot SONRASI degisecek)"
$dg = Get-CimInstance -ClassName Win32_DeviceGuard -Namespace root\Microsoft\Windows\DeviceGuard -ErrorAction SilentlyContinue
if ($dg) {
    Write-Output ("    VirtualizationBasedSecurityStatus = " + $dg.VirtualizationBasedSecurityStatus)
    Write-Output ("    SecurityServicesRunning          = " + ($dg.SecurityServicesRunning -join ','))
}

Write-Output ""
Write-Output "=================================================="
Write-Output "REBOOT GEREKIYOR - kullanici onayi ile shutdown /r /t 0"
Write-Output "=================================================="
