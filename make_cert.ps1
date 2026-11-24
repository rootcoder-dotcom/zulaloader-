# make_cert.ps1 - ZulaDev kod imzalama sertifikasi (MACHINE store)
#
# Recepte (Adim 1): sertifika Cert:\LocalMachine\My'da uretilir, public
# anahtari LocalMachine\Root + LocalMachine\TrustedPublisher'a kurulur.
# Sebep: SCM surucuyu yuklerken sadece MACHINE store'lari gorur; user store'a
# kurulan zincir koptugu icin 577 (invalid image hash) aliniyordu.
#
# makecert KALDIRILDI: yeni WDK'larda yok; New-SelfSignedCertificate (SHA256 + 2048).
# Yonetici ile calistirilmalidir (LocalMachine yazma yetkisi).
$ErrorActionPreference = 'Stop'

try {
    # [1] LocalMachine\My'da CN=ZulaDev (private key'li) var mi?
    $c = Get-ChildItem 'Cert:\LocalMachine\My' -ErrorAction SilentlyContinue |
         Where-Object { $_.Subject -eq 'CN=ZulaDev' -and $_.HasPrivateKey } |
         Sort-Object NotAfter -Descending |
         Select-Object -First 1

    if ($c) {
        Write-Output ("[cert] LocalMachine\My mevcut: " + $c.Thumbprint)
        Write-Output ("[cert] gecerlilik: " + $c.NotBefore + " -> " + $c.NotAfter)
    } else {
        Write-Output "[cert] LocalMachine\My'da yok - uretiliyor..."
        $c = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=ZulaDev' `
                -CertStoreLocation 'Cert:\LocalMachine\My' `
                -HashAlgorithm SHA256 -KeyLength 2048 -KeyExportPolicy Exportable `
                -NotAfter (Get-Date).AddYears(5)
        Write-Output ("[cert] yeni sertifika: " + $c.Thumbprint)
    }

    # [2] public anahtari disa aktar
    $certs = Join-Path $PSScriptRoot 'certs'
    New-Item -ItemType Directory -Force -Path $certs | Out-Null
    $cer = Join-Path $certs 'zula.cer'
    Export-Certificate -Cert $c -FilePath $cer -Force | Out-Null
    Write-Output ("[cert] yazildi: " + $cer)

    # [3] MACHINE Root + TrustedPublisher (kernel zinciri icin zorunlu)
    $r = Import-Certificate -FilePath $cer -CertStoreLocation 'Cert:\LocalMachine\Root'
    $t = Import-Certificate -FilePath $cer -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher'
    Write-Output ("[cert] LocalMachine\Root import: " + $r.Thumbprint)
    Write-Output ("[cert] LocalMachine\TrustedPublisher import: " + $t.Thumbprint)

    # [4] teyit
    $inRoot = @(Get-ChildItem 'Cert:\LocalMachine\Root' | Where-Object { $_.Thumbprint -eq $c.Thumbprint }).Count
    $inPub  = @(Get-ChildItem 'Cert:\LocalMachine\TrustedPublisher' | Where-Object { $_.Thumbprint -eq $c.Thumbprint }).Count
    Write-Output ("[cert] teyit: Root=" + $inRoot + " TrustedPublisher=" + $inPub + " (1/1 olmali)")

    Write-Output ("[cert] THUMBPRINT=" + $c.Thumbprint)
    Write-Output "[cert] tamam"
    exit 0
}
catch {
    Write-Output ("[cert] HATA: " + $_.Exception.Message)
    exit 1
}
