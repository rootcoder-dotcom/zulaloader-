$ts = Get-Date -Format 'HH:mm:ss'
Write-Output ("--- kontrol " + $ts)

$dqSrc = @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class DQ3 {
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern uint QueryDosDevice(string name, StringBuilder target, int max);
}
'@
if (-not ('DQ3' -as [type])) { Add-Type -TypeDefinition $dqSrc -ErrorAction SilentlyContinue }

$sb = New-Object System.Text.StringBuilder 2048
$r = [DQ3]::QueryDosDevice('zuladrv', $sb, 2048)
if ($r -ne 0) {
    Write-Output ("SYMLINK VAR -> " + (($sb.ToString() -split "`0")[0]))
} else {
    Write-Output ("SYMLINK YOK err=" + [Runtime.InteropServices.Marshal]::GetLastWin32Error())
}

try {
    $fs = [IO.File]::Open('\\.\zuladrv', 'Open', 'Read', 'None')
    $fs.Close()
    Write-Output 'CIHAZ ACILDI'
} catch {
    Write-Output ("CIHAZ ACILAMADI: " + $_.Exception.InnerException.Message)
}

# \Device dizininde zuladrv var mi (nesne yoneticiyi tarafi)
$src = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class OM2 {
    [StructLayout(LayoutKind.Sequential)]
    public struct UNICODE_STRING { public ushort Length; public ushort MaximumLength; public IntPtr Buffer; }
    [StructLayout(LayoutKind.Sequential)]
    public struct OBJECT_ATTRIBUTES {
        public int Length; public IntPtr RootDirectory; public IntPtr ObjectName;
        public uint Attributes; public IntPtr SecurityDescriptor; public IntPtr SecurityQualityOfService;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct OBJECT_DIRECTORY_INFORMATION { public UNICODE_STRING Name; public UNICODE_STRING TypeName; }
    [DllImport("ntdll.dll")] static extern int NtOpenDirectoryObject(out IntPtr Handle, uint Access, ref OBJECT_ATTRIBUTES ObjAttr);
    [DllImport("ntdll.dll")] static extern int NtQueryDirectoryObject(IntPtr Handle, IntPtr Buffer, uint Length,
        bool ReturnSingleEntry, bool RestartScan, ref uint Context, ref uint ReturnLength);
    [DllImport("ntdll.dll")] static extern int NtClose(IntPtr Handle);
    static string ReadUS(UNICODE_STRING us) {
        try {
            if (us.Buffer == IntPtr.Zero || us.Length == 0) return "";
            if (us.Length > 1024 || (us.Length % 2) != 0) return "";
            return Marshal.PtrToStringUni(us.Buffer, us.Length / 2);
        } catch { return ""; }
    }
    public static List<string> Find(string dir, string needle) {
        var res = new List<string>();
        IntPtr strBuf = Marshal.AllocHGlobal(2048);
        IntPtr oaName = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(UNICODE_STRING)));
        IntPtr buf = Marshal.AllocHGlobal(65536);
        try {
            Marshal.Copy(new byte[2048], 0, strBuf, 2048);
            Marshal.Copy(dir.ToCharArray(), 0, strBuf, dir.Length);
            var us = new UNICODE_STRING {
                Length = (ushort)(dir.Length * 2),
                MaximumLength = (ushort)(dir.Length * 2 + 2),
                Buffer = strBuf
            };
            Marshal.StructureToPtr(us, oaName, false);
            var oa = new OBJECT_ATTRIBUTES {
                Length = Marshal.SizeOf(typeof(OBJECT_ATTRIBUTES)),
                RootDirectory = IntPtr.Zero, ObjectName = oaName,
                Attributes = 0, SecurityDescriptor = IntPtr.Zero, SecurityQualityOfService = IntPtr.Zero
            };
            IntPtr h;
            int st = NtOpenDirectoryObject(out h, 0x0001, ref oa);
            if (st != 0) { res.Add("open-err 0x" + st.ToString("X8")); return res; }
            try {
                uint ctx = 0, retlen = 0; bool restart = true; int guard = 0; int total = 0;
                while (guard++ < 20000) {
                    int q = NtQueryDirectoryObject(h, buf, 65536, true, restart, ref ctx, ref retlen);
                    restart = false;
                    if (q == unchecked((int)0x8000001A)) break;
                    if (q != 0 && q != 0x105) { res.Add("query-err 0x" + q.ToString("X8")); break; }
                    var di = (OBJECT_DIRECTORY_INFORMATION)Marshal.PtrToStructure(buf, typeof(OBJECT_DIRECTORY_INFORMATION));
                    string nm = ReadUS(di.Name);
                    total++;
                    if (nm.IndexOf(needle, StringComparison.OrdinalIgnoreCase) >= 0) res.Add("HIT " + nm);
                }
                res.Add("toplam=" + total);
            } finally { NtClose(h); }
        } finally {
            Marshal.FreeHGlobal(strBuf); Marshal.FreeHGlobal(oaName); Marshal.FreeHGlobal(buf);
        }
        return res;
    }
}
'@
try {
    Add-Type -TypeDefinition $src -ErrorAction Stop
    foreach ($x in [OM2]::Find('\Device', 'zula')) { Write-Output ("\\Device: " + $x) }
    foreach ($x in [OM2]::Find('\??', 'zula')) { Write-Output ("\\??: " + $x) }
} catch {
    Write-Output ("OM HATASI: " + $_.Exception.Message)
}
