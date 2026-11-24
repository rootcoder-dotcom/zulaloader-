# \\.\zuladrv cihazini exclusive (paylasimsiz) acmayi dener.
# Basarili -> kimse cihazi tutmuyor. Hata 32 (SHARING_VIOLATION) -> bir surec tutuyor.
$sig = @"
using System;
using System.Runtime.InteropServices;
public static class Dev {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr CreateFile(string name, uint access, uint share,
        IntPtr sec, uint disp, uint flags, IntPtr tmpl);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr h);
}
"@
Add-Type -TypeDefinition $sig

# PowerShell 0xC0000000'yi negatif Int32 olarak yorumlar; UInt32'ye bozuk donusum yapar.
# Decimal karsiligi ile veriyoruz (3221225472 = 0xC0000000 = GENERIC_READ|GENERIC_WRITE).
$GEN_RW   = [uint32]3221225472
$SHARE_EX = [uint32]0
$OPEN     = [uint32]3

$INVALID = [IntPtr](-1)
$h = [Dev]::CreateFile("\\.\zuladrv", $GEN_RW, $SHARE_EX, [IntPtr]::Zero, $OPEN, [uint32]0, [IntPtr]::Zero)
$err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()

if ($h -eq $INVALID -or $h -eq [IntPtr]::Zero) {
    if ($err -eq 32) {
        Write-Output "CIHAZ TUTULUYOR: baska bir surec \\.\zuladrv handle'i acik tutuyor (hata 32 SHARING_VIOLATION)"
    } elseif ($err -eq 2) {
        Write-Output "CIHAZ YOK: \\.\zuladrv bulunamadi (hata 2) - surucu yuklu degil veya cihaz silinmis"
    } elseif ($err -eq 5) {
        Write-Output "ERISIM REDDEDILDI (hata 5) - surucu yuklu ama yetki yok"
    } else {
        Write-Output ("CIHAZ ACILAMADI, Win32 hata: " + $err)
    }
} else {
    Write-Output "CIHAZ BOS: kimse tutmuyor (exclusive acildi)"
    [Dev]::CloseHandle($h) | Out-Null
}
