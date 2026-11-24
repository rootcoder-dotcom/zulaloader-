$out = @()
$out += "=== surec listesi ==="
$out += (Get-Process | Where-Object { $_.ProcessName -match 'loader|test_driver|mmc|services|zula' } |
    Select-Object ProcessName, Id, StartTime | Format-Table -AutoSize | Out-String)
$out += "=== servis durumu ==="
$out += (& sc.exe query zuladrv 2>&1 | Out-String)
$out += (& sc.exe qc zuladrv 2>&1 | Out-String)
$out += "=== acik cihaz handle (zuladrv) aranıyor ==="
$out += (Get-Process | ForEach-Object { $_ } | Measure-Object | Select-Object -ExpandProperty Count)
$out += "toplam surec sayisi (yukaridaki)"
$out | Out-File -FilePath "$PSScriptRoot\diag_log.txt" -Encoding utf8
Write-Output "yazildi: diag_log.txt"
