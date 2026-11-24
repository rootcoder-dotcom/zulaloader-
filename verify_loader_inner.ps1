# Bu script YUKSEK YETKILI pencere icinde calisir (runas ile yukseltilmis).
# loader.exe'yi ciktiyla baslatir, 12 sn bekler, raporlar ve loader'i durdurur
# (teardown() servisi STOP+DELETE yaptigi icin erken sonlandirma gerekli).
$out = 'C:\Users\Baran\Desktop\zula\loader_verify.txt'
$err = 'C:\Users\Baran\Desktop\zula\loader_verify_err.txt'
$rep = 'C:\Users\Baran\Desktop\zula\loader_verify_report.txt'
Remove-Item $out, $err -ErrorAction SilentlyContinue

$p = Start-Process -FilePath 'C:\zula\loader.exe' -PassThru `
    -RedirectStandardOutput $out -RedirectStandardError $err
Write-Output ('loader pid: ' + $p.Id)

Start-Sleep -Seconds 12

Write-Output '--- loader cikti ---'
if (Test-Path $out) { Get-Content $out }
if (Test-Path $err) { Get-Content $err }

Get-Process -Name loader -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Output ('[pid ' + $_.Id + ' durduruluyor - teardown calismasin diye]')
    Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
}

Write-Output '--- bitti ---'
