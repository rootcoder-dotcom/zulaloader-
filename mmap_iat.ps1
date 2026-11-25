$src = @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class MMap {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true, ExactSpelling=true)]
    public static extern SafeFileHandle CreateFileW(string n, uint a, uint s, IntPtr sec, uint d, uint f, IntPtr t);

    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool DeviceIoControl(SafeFileHandle h, uint code, byte[] inBuf, uint inLen, byte[] outBuf, uint outLen, out uint ret, IntPtr ov);

    static SafeFileHandle h;

    public static void Connect() {
        h = CreateFileW("\\\\.\\GLOBALROOT\\Device\\zuladrv", 0xC0000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (h.IsInvalid) throw new Exception("cihaz acilamadi err=" + Marshal.GetLastWin32Error());
        var v = new byte[8];
        uint ret;
        if (!DeviceIoControl(h, 0x00222000, null, 0, v, 8, out ret, IntPtr.Zero))
            throw new Exception("verify err=" + Marshal.GetLastWin32Error());
        Console.WriteLine("verify OK");
    }

    static byte[] Io(uint code, byte[] inBuf, int outLen) {
        var outBuf = new byte[outLen];
        uint ret;
        if (!DeviceIoControl(h, code, inBuf, (uint)inBuf.Length, outBuf, (uint)outBuf.Length, out ret, IntPtr.Zero))
            throw new Exception("IOCTL 0x" + code.ToString("X") + " err=" + Marshal.GetLastWin32Error());
        return outBuf;
    }

    public static ulong GetModule(uint pid, string name) {
        var b = new byte[152];
        BitConverter.GetBytes(pid).CopyTo(b, 0);
        var nb = System.Text.Encoding.Unicode.GetBytes(name);
        Array.Copy(nb, 0, b, 8, Math.Min(nb.Length, 126));
        var r = Io(0x0022200C, b, 152);
        return BitConverter.ToUInt64(r, 136);
    }

    public static ulong Alloc(uint pid, ulong size, uint prot) {
        var b = new byte[32];
        BitConverter.GetBytes(pid).CopyTo(b, 0);
        BitConverter.GetBytes(size).CopyTo(b, 8);
        BitConverter.GetBytes(prot).CopyTo(b, 16);
        var r = Io(0x00222010, b, 32);
        ulong addr = BitConverter.ToUInt64(r, 24);
        if (addr == 0) throw new Exception("alloc adres=0");
        return addr;
    }

    public static void Write(uint pid, ulong addr, byte[] data) {
        var b = new byte[24 + data.Length];
        BitConverter.GetBytes(pid).CopyTo(b, 0);
        BitConverter.GetBytes(addr).CopyTo(b, 8);
        BitConverter.GetBytes((ulong)data.Length).CopyTo(b, 16);
        data.CopyTo(b, 24);
        Io(0x00222008, b, 0);
    }

    public static byte[] Read(uint pid, ulong addr, int n) {
        var b = new byte[24];
        BitConverter.GetBytes(pid).CopyTo(b, 0);
        BitConverter.GetBytes(addr).CopyTo(b, 8);
        BitConverter.GetBytes((ulong)n).CopyTo(b, 16);
        var r = Io(0x00222004, b, 24 + n);
        var o = new byte[n];
        Array.Copy(r, 24, o, 0, n);
        return o;
    }

    public static string ManualMap(uint pid, ulong va, ulong size) {
        var b = new byte[176];
        BitConverter.GetBytes(pid).CopyTo(b, 0);
        BitConverter.GetBytes(va).CopyTo(b, 8);
        BitConverter.GetBytes(size).CopyTo(b, 16);
        var r = Io(0x00222018, b, 176);
        ulong baseA = BitConverter.ToUInt64(r, 152);
        ulong entry  = BitConverter.ToUInt64(r, 160);
        if (baseA == 0 || entry == 0) throw new Exception("map basarisiz");
        return baseA.ToString("X");
    }
}
'@

Add-Type -TypeDefinition $src -ErrorAction Stop

# --- PE import tam liste: (modul, fonksiyon) -> IAT slot RVA ---
$b = [IO.File]::ReadAllBytes("C:\zula\cheat.dll")
$pe = [BitConverter]::ToInt32($b, 0x3C)
$nsec = [BitConverter]::ToUInt16($b, $pe + 6)
$soh = [BitConverter]::ToUInt16($b, $pe + 20)
$opt = $pe + 24
$impRva = [BitConverter]::ToUInt32($b, ($opt + 112) + 8)
$secs = @()
$so = $opt + $soh
for ($i = 0; $i -lt $nsec; $i++) {
    $o = $so + $i * 40
    $secs += ,([pscustomobject]@{
        VA = [BitConverter]::ToUInt32($b, $o + 12); VS = [BitConverter]::ToUInt32($b, $o + 8)
        RP = [BitConverter]::ToUInt32($b, $o + 20); RS = [BitConverter]::ToUInt32($b, $o + 16)
    })
}
function Rva2Off([uint32]$rva) {
    foreach ($s in $secs) {
        $sz = [Math]::Max($s.VS, $s.RS)
        if ($rva -ge $s.VA -and $rva -lt ($s.VA + $sz)) { return [int]($rva - $s.VA + $s.RP) }
    }
    return -1
}
function ReadStr([int]$off) {
    $e = $off
    while ($b[$e] -ne 0) { $e++ }
    return [Text.Encoding]::ASCII.GetString($b, $off, $e - $off)
}

$want = @("CreateFileA", "WriteFile", "wsprintfA", "GetModuleHandleA", "D3D11CreateDeviceAndSwapChain", "MH_Initialize")
$slots = @{}
$off = Rva2Off $impRva
while ($off -ge 0) {
    $nameRva = [BitConverter]::ToUInt32($b, $off + 12)
    if ($nameRva -eq 0) { break }
    $oft = [BitConverter]::ToUInt32($b, $off)
    $ft  = [BitConverter]::ToUInt32($b, $off + 16)
    $mod = ReadStr (Rva2Off $nameRva)
    $t = if ($oft -ne 0) { $oft } else { $ft }
    $i = 0
    while ($true) {
        $tv = [BitConverter]::ToUInt64($b, (Rva2Off $t) + $i * 8)
        if ($tv -eq 0) { break }
        if (($tv -band 0x8000000000000000) -eq 0) {
            $fn = ReadStr ((Rva2Off ([uint32]$tv)) + 2)
            if ($want -contains $fn) { $slots[$fn] = @{ mod = $mod; rva = [uint32]($ft + $i * 8) } }
        }
        $i++
        if ($i -gt 3000) { break }
    }
    $off += 20
}
foreach ($k in $slots.Keys) { echo ("slot: " + $k + " <- " + $slots[$k].mod + " @RVA 0x" + $slots[$k].rva.ToString("X")) }

# --- hedef ---
$z = Get-Process -Name zula -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $z) { echo "zula yok - baslatiliyor"; Start-Process -FilePath "C:\zula\Game\zula.exe" -WorkingDirectory "C:\zula\Game"; Start-Sleep -Seconds 6; $z = Get-Process -Name zula -ErrorAction SilentlyContinue | Select-Object -First 1 }
if (-not $z) { echo "zula baslatilamadi"; exit 1 }
$pid2 = [uint32]$z.Id
echo ("hedef pid=" + $pid2)

[MMap]::Connect()

# --- FAZ A: mod_base (GET_MODULE) ---
foreach ($mn in @("kernel32.dll", "user32.dll", "ntdll.dll", "d3d11.dll")) {
    $gb = [MMap]::GetModule($pid2, $mn)
    echo ("GET_MODULE " + $mn + " -> 0x" + $gb.ToString("X") + $(if ($gb -eq 0) { "  (BULUNAMADI)" } else { "  (OK)" }))
}

# --- FAZ B: map + IAT okuma ---
$dll = $b
$va = [MMap]::Alloc($pid2, [uint64]$dll.Length, [uint32]0x40)
[MMap]::Write($pid2, $va, $dll)
$baseHex = [MMap]::ManualMap($pid2, $va, [uint64]$dll.Length)
$base = [Convert]::ToUInt64($baseHex, 16)
echo ("map base=0x" + $baseHex)

foreach ($k in @("CreateFileA", "wsprintfA", "GetModuleHandleA", "D3D11CreateDeviceAndSwapChain")) {
    if (-not $slots.ContainsKey($k)) { echo ($k + ": slot yok"); continue }
    $raw = [MMap]::Read($pid2, ($base + $slots[$k].rva), 8)
    $val = [BitConverter]::ToUInt64($raw, 0)
    $st = if ($val -gt 0x7FF000000000) { "COZULDU" } elseif ($val -eq 0) { "BOS" } else { "COZULMEDI (0x" + $val.ToString("X") + ")" }
    echo ("IAT " + $k.PadRight(32) + " = 0x" + $val.ToString("X16") + "  " + $st)
}
echo "(thread olusturulmadi - zula ayakta kaldi)"
