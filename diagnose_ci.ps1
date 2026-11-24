# diagnose_ci.ps1 - neden 577 (invalid image hash)? (yuksek yetkili calismalidir)
$ErrorActionPreference = 'Continue'

function Short([string]$s, [int]$n) {
    $s = ($s -replace '\s+', ' ').Trim()
    if ($s.Length -gt $n) { return $s.Substring(0, $n) }
    return $s
}

Write-Output "=================================================="
Write-Output ("diagnose_ci - " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Write-Output "=================================================="

Write-Output ""
Write-Output "[1] sc start zuladrv (yeniden dene)"
cmd /c "sc start zuladrv" 2>&1 | ForEach-Object { Write-Output ("    " + $_) }
Write-Output ("    exit=" + $LASTEXITCODE)
cmd /c "sc query zuladrv" 2>&1 | Select-String "STATE|WIN32_EXIT" | ForEach-Object { Write-Output ("    " + $_) }

Write-Output ""
Write-Output "[2] CodeIntegrity event log (son 15 kayit)"
try {
    $ev = Get-WinEvent -LogName 'Microsoft-Windows-CodeIntegrity/Operational' -MaxEvents 15 -ErrorAction Stop
    foreach ($e in $ev) {
        Write-Output ("    " + $e.TimeCreated.ToString('MM-dd HH:mm:ss') + " [" + $e.Id + "] " + (Short $e.Message 400))
    }
    if (-not $ev) { Write-Output "    (kayit yok)" }
} catch {
    Write-Output ("    okunamadi: " + $_.Exception.Message)
}

Write-Output ""
Write-Output "[3] System log - servis/hata kayitlari (son 10, 7000/7045/219)"
try {
    $se = Get-WinEvent -LogName System -MaxEvents 200 -ErrorAction Stop |
          Where-Object { $_.Id -in 7000,7045,219,7026 } | Select-Object -First 10
    foreach ($e in $se) {
        Write-Output ("    " + $e.TimeCreated.ToString('MM-dd HH:mm:ss') + " [" + $e.Id + "] " + (Short $e.Message 300))
    }
    if (-not $se) { Write-Output "    (kayit yok)" }
} catch {
    Write-Output ("    okunamadi: " + $_.Exception.Message)
}

Write-Output ""
Write-Output "[4] Smart App Control / CI policy durumu"
$sac = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy' -ErrorAction SilentlyContinue
if ($sac) {
    Write-Output ("    VerifiedAndReputablePolicyState = " + $sac.VerifiedAndReputablePolicyState + "  (0=kapali 1=enforce 2=audit)")
} else {
    Write-Output "    CI\Policy anahtari yok (SAC kapali/gorunmuyor)"
}
$cic = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Config' -ErrorAction SilentlyContinue
if ($cic) { Write-Output ("    CI\Config: " + (($cic.PSObject.Properties | Where-Object {$_.Name -notlike 'PS*'} | ForEach-Object { $_.Name + '=' + $_.Value }) -join ' ')) }
else { Write-Output "    CI\Config anahtari yok" }

Write-Output ""
Write-Output "[5] VBS / boot ayarlari teyidi"
$dg = Get-CimInstance -ClassName Win32_DeviceGuard -Namespace root\Microsoft\Windows\DeviceGuard -ErrorAction SilentlyContinue
if ($dg) {
    Write-Output ("    VirtualizationBasedSecurityStatus = " + $dg.VirtualizationBasedSecurityStatus + " (2=VBS calisiyor)")
    Write-Output ("    SecurityServicesRunning          = " + ($dg.SecurityServicesRunning -join ',') + " (2=HVCI)")
    Write-Output ("    CodeIntegrityPolicyEnforcement   = " + $dg.CodeIntegrityPolicyEnforcementStatus + " (0=kapali 1=audit 2=enforce)")
}
cmd /c "bcdedit /enum {current}" 2>&1 | Select-String "testsigning|nointegritychecks|integrityservices|debug" | ForEach-Object { Write-Output ("    " + $_) }

Write-Output ""
Write-Output "[6] Machine Root'ta ZulaDev (kernel CI zinciri icin)"
$roots = @(Get-ChildItem 'Cert:\LocalMachine\Root' -ErrorAction SilentlyContinue | Where-Object { $_.Subject -eq 'CN=ZulaDev' })
Write-Output ("    machine Root ZulaDev sayisi = " + $roots.Count)
foreach ($r in $roots) { Write-Output ("      " + $r.Thumbprint) }

Write-Output ""
Write-Output "=================================================="
Write-Output "bitti"
Write-Output "=================================================="
