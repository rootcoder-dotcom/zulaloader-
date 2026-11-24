# prepare_kdmapper.ps1 - kdmapper on hazirligi (yuksek yetkili)
# [1] Defender exclusion (kdmapper.exe "kotu yazilim" imzasiyla engellendi)
# [2] Vulnerable Driver Blocklist kapat (iqvw64e icin sart - kdmapper README)
# [3] eski servis kaydini sil (BE servis girdisini gormesin - Yol A maddesi 4)
# [4] surucuyu yeniden imzala (sign.bat: machine store + verify)
$ErrorActionPreference = 'Continue'

Write-Output "=================================================="
Write-Output ("prepare_kdmapper - " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Write-Output "=================================================="

Write-Output ""
Write-Output "[1] Defender exclusion"
$paths = @('C:\Users\Baran\Desktop\zula', 'C:\zula')
foreach ($p in $paths) {
    try {
        Add-MpPreference -ExclusionPath $p -ErrorAction Stop
        Write-Output ("    eklendi: " + $p)
    } catch {
        Write-Output ("    HATA " + $p + ": " + $_.Exception.Message)
    }
}
Write-Output ("    mevcut exclusion: " + ((Get-MpPreference).ExclusionPath -join ' | '))

Write-Output ""
Write-Output "[2] Vulnerable Driver Blocklist = 0"
$k = 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Config'
New-ItemProperty -Path $k -Name 'VulnerableDriverBlocklistEnable' -PropertyType DWord -Value 0 -Force | Out-Null
Write-Output ("    deger = " + (Get-ItemProperty -Path $k -Name 'VulnerableDriverBlocklistEnable').VulnerableDriverBlocklistEnable)

Write-Output ""
Write-Output "[3] eski servis kaydini sil"
cmd /c "sc stop zuladrv" 2>&1 | ForEach-Object { Write-Output ("    " + $_) }
cmd /c "sc delete zuladrv" 2>&1 | ForEach-Object { Write-Output ("    " + $_) }
$N = 0
while ($N -lt 20) {
    $q = cmd /c "sc query zuladrv" 2>&1
    if ($LASTEXITCODE -ne 0) { break }
    Start-Sleep -Seconds 1
    $N++
}
Write-Output ("    kayit silindi (bekleme=" + $N + "sn, exit=" + $LASTEXITCODE + ")")

Write-Output ""
Write-Output "[4] surucu imzalama (sign.bat)"
cmd /c "cd /d C:\Users\Baran\Desktop\zula && call sign.bat" 2>&1 | ForEach-Object { Write-Output ("    " + $_) }
Write-Output ("    sign exit=" + $LASTEXITCODE)

Write-Output ""
Write-Output "=================================================="
Write-Output "bitti - reboot gerekiyor (blocklist icin)"
Write-Output "=================================================="
