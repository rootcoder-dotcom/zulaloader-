try {
    Start-Process -FilePath 'C:\Users\Baran\Desktop\zula\run_deploy.bat' -Verb RunAs -Wait -ErrorAction Stop
    Write-Output 'ELEVATED TAMAMLANDI'
} catch {
    Write-Output ('UAC HATASI: ' + $_.Exception.Message)
}
Write-Output ''
Write-Output 'Cikti: C:\Users\Baran\Desktop\zula\deploy_log.txt'
Write-Output ''
pause
