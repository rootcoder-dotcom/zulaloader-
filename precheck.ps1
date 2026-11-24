# precheck.ps1 - bcdedit/HVCI/SecureBoot/BitLocker durum tespiti (yuksek yetkili calismalidir)
$ErrorActionPreference = 'Continue'
$sc = {
    param($t)
    Write-Output ""
    Write-Output ("==== " + $t)
}

& $sc "bcdedit /enum {current}"
cmd /c "bcdedit /enum {current}" 2>&1

& $sc "SecureBoot (UEFI)"
try { $sb = Confirm-SecureBootUEFI; Write-Output ("SecureBoot = " + $sb) }
catch { Write-Output ("SecureBoot OKUNAMADI: " + $_.Exception.Message) }

& $sc "VBS / HVCI (Bellek butunlugu)"
$dg = Get-CimInstance -ClassName Win32_DeviceGuard -Namespace root\Microsoft\Windows\DeviceGuard -ErrorAction SilentlyContinue
if ($dg) {
    Write-Output ("VirtualizationBasedSecurityStatus (0=kapali 1=baslatilmis 2=calisiyor) = " + $dg.VirtualizationBasedSecurityStatus)
    Write-Output ("SecurityServicesConfigured            = " + ($dg.SecurityServicesConfigured -join ','))
    Write-Output ("SecurityServicesRunning               = " + ($dg.SecurityServicesRunning -join ',') + "  (2 = HVCI/Bellek butunlugu CALISIYOR)")
    Write-Output ("CodeIntegrityPolicyEnforcementStatus  = " + $dg.CodeIntegrityPolicyEnforcementStatus)
} else { Write-Output "Win32_DeviceGuard okunamadi" }

$hv = Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity" -ErrorAction SilentlyContinue
if ($hv) { Write-Output ("HK HVCI Enabled = " + $hv.Enabled) } else { Write-Output "HK HVCI anahtari yok (varsayilan)" }
$vg = Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Control\DeviceGuard" -ErrorAction SilentlyContinue
if ($vg) { Write-Output ("HK DeviceGuard EnableVirtualizationBasedSecurity = " + $vg.EnableVirtualizationBasedSecurity + " / RequirePlatformSecurityFeatures = " + $vg.RequirePlatformSecurityFeatures) }

& $sc "Driver Verifier"
cmd /c "verifier /query" 2>&1

& $sc "BitLocker C:"
try { Get-BitLockerVolume -MountPoint C: -ErrorAction Stop | Format-List MountPoint,VolumeStatus,ProtectionStatus,LockStatus,EncryptionPercentage }
catch { Write-Output ("BitLocker OKUNAMADI: " + $_.Exception.Message) }

& $sc "Mevcut servis zuladrv"
cmd /c "sc query zuladrv" 2>&1
cmd /c "sc qc zuladrv" 2>&1

Write-Output ""
Write-Output "==== bitti"
