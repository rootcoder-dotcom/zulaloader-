# NtUnloadDriver ile surucuyu SCM disi yukler (SeLoadDriverPrivilege gerekir).
# Cikti: NTSTATUS (0 = basarili) - repair2.bat yonlendirir.
$sig = @"
using System;
using System.Runtime.InteropServices;
public static class Native {
    [StructLayout(LayoutKind.Sequential)]
    public struct UNICODE_STRING { public ushort Length; public ushort MaximumLength; public IntPtr Buffer; }
    [StructLayout(LayoutKind.Sequential)]
    public struct LUID { public uint LowPart; public int HighPart; }
    [StructLayout(LayoutKind.Sequential)]
    public struct TOKEN_PRIVILEGES { public int PrivilegeCount; public LUID Luid; public int Attributes; }

    [DllImport("ntdll.dll")]
    public static extern int NtUnloadDriver(ref UNICODE_STRING DriverServiceName);

    [DllImport("advapi32.dll", SetLastError = true)]
    public static extern bool OpenProcessToken(IntPtr ProcessHandle, uint DesiredAccess, out IntPtr TokenHandle);

    [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern bool LookupPrivilegeValue(string SystemName, string Name, out LUID Luid);

    [DllImport("advapi32.dll", SetLastError = true)]
    public static extern bool AdjustTokenPrivileges(IntPtr TokenHandle, bool DisableAllPrivileges,
        ref TOKEN_PRIVILEGES NewState, int BufferLength, IntPtr PreviousState, IntPtr ReturnLength);

    [DllImport("kernel32.dll")]
    public static extern IntPtr GetCurrentProcess();

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr hObject);
}
"@
Add-Type -TypeDefinition $sig

$TOKEN_QUERY = 0x0008
$TOKEN_ADJUST_PRIVILEGES = 0x0020
$SE_PRIVILEGE_ENABLED = 0x0002

$tok = [IntPtr]::Zero
if (-not [Native]::OpenProcessToken([Native]::GetCurrentProcess(), ($TOKEN_QUERY -bor $TOKEN_ADJUST_PRIVILEGES), [ref]$tok)) {
    Write-Output ("OpenProcessToken HATASI: " + [Runtime.InteropServices.Marshal]::GetLastWin32Error())
    exit 1
}

$luid = New-Object Native+LUID
if (-not [Native]::LookupPrivilegeValue($null, "SeLoadDriverPrivilege", [ref]$luid)) {
    Write-Output ("LookupPrivilegeValue HATASI: " + [Runtime.InteropServices.Marshal]::GetLastWin32Error())
    [Native]::CloseHandle($tok) | Out-Null
    exit 1
}

$tp = New-Object Native+TOKEN_PRIVILEGES
$tp.PrivilegeCount = 1
$tp.Luid = $luid
$tp.Attributes = $SE_PRIVILEGE_ENABLED

# Uyari: AdjustTokenPrivileges basarili olsa bile son hata degeri eski kalabilir;
# sadece donus degeri false ise fatal say, 1300 (NOT_ALL_ASSIGNED) disindaki
# degerleri not et ve yine de unload dene.
$adjOk = [Native]::AdjustTokenPrivileges($tok, $false, [ref]$tp, 0, [IntPtr]::Zero, [IntPtr]::Zero)
$adjErr = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
[Native]::CloseHandle($tok) | Out-Null
if (-not $adjOk) {
    Write-Output ("AdjustTokenPrivileges DONUS false, HATA: " + $adjErr)
    exit 1
}
if ($adjErr -eq 1300) {
    Write-Output "HATA: SeLoadDriverPrivilege atanamadi (1300 NOT_ALL_ASSIGNED)"
    exit 1
}
Write-Output ("SeLoadDriverPrivilege etkinlestirildi (son hata degeri: " + $adjErr + ")")

$path = "\Registry\Machine\System\CurrentControlSet\Services\zuladrv"
$ptr = [Runtime.InteropServices.Marshal]::StringToHGlobalUni($path)
$us = New-Object Native+UNICODE_STRING
$us.Length = [uint16]($path.Length * 2)
$us.MaximumLength = [uint16]($path.Length * 2 + 2)
$us.Buffer = $ptr

$st = [Native]::NtUnloadDriver([ref]$us)
[Runtime.InteropServices.Marshal]::FreeHGlobal($ptr)

Write-Output ("NtUnloadDriver NTSTATUS: 0x{0:X8}" -f $st)
if ($st -eq 0) { Write-Output "OK: surucu yukden indirildi" } else { Write-Output "HATA: surucu indirilemedi" }
exit 0
