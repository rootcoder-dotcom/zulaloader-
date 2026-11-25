$src = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class PebWalk {
    [DllImport("ntdll.dll")]
    static extern int NtQueryInformationProcess(IntPtr h, int cls, ref PBI pbi, int len, out int retLen);

    [StructLayout(LayoutKind.Sequential)]
    struct PBI { public IntPtr r1; public IntPtr PebBaseAddress; public IntPtr r2; public IntPtr r3; public IntPtr UniquePid; public IntPtr r4; }

    [DllImport("kernel32.dll", SetLastError=true)]
    static extern IntPtr OpenProcess(uint access, bool inherit, int pid);

    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, int size, out int read);

    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr h);

    static IntPtr hProc;
    static byte[] Read(IntPtr addr, int n) {
        var b = new byte[n];
        int got;
        if (!ReadProcessMemory(hProc, addr, b, n, out got) || got != n)
            throw new Exception("okuma basarisiz @0x" + addr.ToString("X") + " err=" + Marshal.GetLastWin32Error());
        return b;
    }

    public static string[] Walk(int pid) {
        var res = new List<string>();
        hProc = OpenProcess(0x0410, false, pid);   // QUERY_INFORMATION|VM_READ
        if (hProc == IntPtr.Zero) throw new Exception("OpenProcess err=" + Marshal.GetLastWin32Error());
        try {
            var pbi = new PBI();
            int rl;
            int st = NtQueryInformationProcess(hProc, 0, ref pbi, Marshal.SizeOf(typeof(PBI)), out rl);
            if (st != 0 || pbi.PebBaseAddress == IntPtr.Zero)
                throw new Exception("NtQueryInformationProcess st=0x" + st.ToString("X"));
            IntPtr peb = pbi.PebBaseAddress;
            IntPtr ldr = new IntPtr(BitConverter.ToInt64(Read(IntPtr.Add(peb, 0x18), 8), 0));
            if (ldr == IntPtr.Zero) { res.Add("PEB.Ldr = NULL"); return res.ToArray(); }
            IntPtr head = IntPtr.Add(ldr, 0x10);
            IntPtr e = new IntPtr(BitConverter.ToInt64(Read(head, 8), 0));
            int idx = 0;
            while (e != IntPtr.Zero && e != head && idx < 200) {
                byte[] ent = Read(e, 0x70);
                ulong dllBase = BitConverter.ToUInt64(ent, 0x30);
                ushort nlen = BitConverter.ToUInt16(ent, 0x58);
                ulong nameBuf = BitConverter.ToUInt64(ent, 0x60);
                string name = "?";
                if (nameBuf != 0 && nlen > 0 && nlen < 520) {
                    try { name = System.Text.Encoding.Unicode.GetString(Read(new IntPtr((long)nameBuf), nlen)); }
                    catch (Exception ex) { name = "<ad okunamadi: " + ex.Message + ">"; }
                }
                res.Add(String.Format("{0,-2} {1,-40} base=0x{2:X}", idx, name, dllBase));
                IntPtr next;
                try { next = new IntPtr(BitConverter.ToInt64(Read(e, 8), 0)); }
                catch { res.Add("    !! Flink okunamadi -> yuruyus burada kopuyor"); break; }
                if (next == e) { res.Add("    !! Flink kendisini gosteriyor -> kopuk liste"); break; }
                e = next;
                idx++;
            }
            return res.ToArray();
        } finally { CloseHandle(hProc); }
    }
}
'@

Add-Type -TypeDefinition $src -ErrorAction Stop

$z = Get-Process -Name zula -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $z) { echo "zula yok"; exit 1 }
echo ("hedef pid=" + $z.Id)
echo "==== PEB InLoadOrderModuleList (hedef gercegi):"
try {
    [PebWalk]::Walk($z.Id) | ForEach-Object { echo ("  " + $_) }
} catch {
    echo ("HATA: " + $_.Exception.Message)
}
