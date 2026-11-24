# make_cert.ps1 - ZulaDev kod imzalama sertifikasi
#
# makecert KALDIRILDI: yeni WDK'larda (10.0.26100) yok ve varsayilani
# 1024-bit RSA + SHA1 - imza zinciri dogrulamasinda zayif kaliyor.
# Yerine New-SelfSignedCertificate: SHA256 + 2048-bit RSA.
#
# sign.bat bunu cagirir; imzalama /n ZulaDev ile yapilir (sabit parmak izi degil).
# Store'da birden fazla ZulaDev olabilir; signtool /a ile en iyisini secer, o
# yuzden bulunan TUM sertifikalarin public anahtari Root'a kurulur ki secilen
# her ne olursa olsun zincir guvenilir bitsin.
#
# Kurulum harici certutil YERINE X509Store API ile yapilir (surec beklemez).
$ErrorActionPreference = 'Stop'
$storeName = 'PrivateCertStore'   # sign.bat'in /s ile kullandigi ayni store

function Get-ZulaAll {
    $locs = @(
        ('Cert:\CurrentUser\'  + $storeName),
        ('Cert:\LocalMachine\' + $storeName),
        'Cert:\CurrentUser\My',
        'Cert:\LocalMachine\My'
    )
    $seen = @{}
    $res = @()
    foreach ($l in $locs) {
        Get-ChildItem $l -ErrorAction SilentlyContinue |
            Where-Object { $_.Subject -eq 'CN=ZulaDev' -and $_.HasPrivateKey } |
            ForEach-Object {
                if (-not $seen.ContainsKey($_.Thumbprint)) {
                    $seen[$_.Thumbprint] = $true
                    $res += $_
                }
            }
    }
    return $res
}

function Add-ToStore([System.Security.Cryptography.X509Certificates.X509Certificate2]$cert,
                     [string]$storeName2, [string]$location) {
    $st = New-Object System.Security.Cryptography.X509Certificates.X509Store($storeName2, $location)
    try {
        $st.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
        $st.Add($cert)
        $st.Close()
        return $true
    } catch {
        try { $st.Close() } catch { }
        return $false
    }
}

try {
    Write-Output "[cert] store taraniyor..."
    $list = @(Get-ZulaAll)

    if ($list.Count -eq 0) {
        Write-Output "[cert] mevcut yok - New-SelfSignedCertificate ile uretiliyor..."
        $loc = 'Cert:\CurrentUser\' + $storeName
        $c = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=ZulaDev' `
                -HashAlgorithm SHA256 -KeyLength 2048 -KeyExportPolicy Exportable `
                -NotAfter (Get-Date).AddYears(5) -CertStoreLocation $loc
        Write-Output ("[cert] yeni sertifika: " + $c.Thumbprint)
        $list = @($c)
    } else {
        Write-Output ("[cert] " + $list.Count + " adet ZulaDev sertifikasi bulundu:")
        foreach ($c in $list) { Write-Output ("[cert]   " + $c.Thumbprint + "  (gecerlilik " + $c.NotAfter + ")") }
    }

    $machineOk = $true
    foreach ($c in $list) {
        # $c zaten Cert:\ surucusunden gelen X509Certificate2
        $r1 = Add-ToStore $c 'Root' 'LocalMachine'
        $r2 = Add-ToStore $c 'TrustedPublisher' 'LocalMachine'
        if (-not ($r1 -and $r2)) { $machineOk = $false }

        Write-Output ("[cert] " + $c.Thumbprint + " -> machine Root=" + $r1 + " TrustedPublisher=" + $r2)
    }

    if (-not $machineOk) {
        Write-Output "[cert] UYARI: machine store yetkisi yok, user store'a dusuluyor."
        Write-Output "[cert]        Kernel surucu icin machine store gerekir - sign.bat'i yonetici ile calistirin."
        foreach ($c in $list) {
            [void](Add-ToStore $c 'Root' 'CurrentUser')
            [void](Add-ToStore $c 'TrustedPublisher' 'CurrentUser')
        }
    } else {
        Write-Output "[cert] Root + TrustedPublisher (machine) kuruldu"
    }

    # README ile uyumlu tek dosya
    $certs = Join-Path $PSScriptRoot 'certs'
    New-Item -ItemType Directory -Force -Path $certs | Out-Null
    Export-Certificate -Cert $list[0] -FilePath (Join-Path $certs 'zula.cer') -Force | Out-Null
    Write-Output ("[cert] yazildi: " + (Join-Path $certs 'zula.cer'))

    Write-Output "[cert] tamam"
    exit 0
}
catch {
    Write-Output ("[cert] HATA: " + $_.Exception.Message)
    exit 1
}
