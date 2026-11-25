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
        uint magic = BitConverter.ToUInt32(v, 0), ver = BitConverter.ToUInt32(v, 4);
        if (magic != 0x5A554C41u) throw new Exception("magic yanlis 0x" + magic.ToString("X"));
        Console.WriteLine("verify OK ver=0x" + ver.ToString("X"));
    }

    static byte[] Io(uint code, byte[] inBuf, int outLen) {
        var outBuf = new byte[outLen];
        uint ret;
        if (!DeviceIoControl(h, code, inBuf, (uint)inBuf.Length, outBuf, (uint)outBuf.Length, out ret, IntPtr.Zero))
            throw new Exception("IOCTL 0x" + code.ToString("X") + " err=" + Marshal.GetLastWin32Error());
        return outBuf;
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
        var out2 = new byte[n];
        Array.Copy(r, 24, out2, 0, n);
        return out2;
    }

    public static string ManualMap(uint pid, ulong va, ulong size) {
        var b = new byte[176];
        BitConverter.GetBytes(pid).CopyTo(b, 0);
        BitConverter.GetBytes(va).CopyTo(b, 8);
        BitConverter.GetBytes(size).CopyTo(b, 16);
        var r = Io(0x00222018, b, 176);
        ulong baseA = BitConverter.ToUInt64(r, 152);
        ulong entry  = BitConverter.ToUInt64(r, 160);
        ulong isz    = BitConverter.ToUInt64(r, 168);
        if (baseA == 0 || entry == 0) throw new Exception("map basarisiz base=0");
        return "base=0x" + baseA.ToString("X") + " entry=0x" + entry.ToString("X") + " size=0x" + isz.ToString("X");
    }

    public static void CreateThread(uint pid, ulong start, ulong param) {
        var b = new byte[32];
        BitConverter.GetBytes(pid).CopyTo(b, 0);
        BitConverter.GetBytes(start).CopyTo(b, 8);
        BitConverter.GetBytes(param).CopyTo(b, 16);
        Io(0x00222014, b, 0);
    }

    public static string Hex(byte[] d) {
        var s = new System.Text.StringBuilder();
        foreach (var x in d) s.Append(x.ToString("X2"));
        return s.ToString();
    }
}
'@

Add-Type -TypeDefinition $src -ErrorAction Stop

# --- PE import tablosu: CreateFileA IAT slot'unun RVA'si (cheat.dll uzerinden) ---
$b = [IO.File]::ReadAllBytes("C:\zula\cheat.dll")
$pe = [BitConverter]::ToInt32($b, 0x3C)
$nsec = [BitConverter]::ToUInt16($b, $pe + 6)
$soh = [BitConverter]::ToUInt16($b, $pe + 20)
$opt = $pe + 24
$dd = $opt + 112
$impRva = [BitConverter]::ToUInt32($b, $dd + 8)
$secs = @()
$so = $opt + $soh
for ($i = 0; $i -lt $nsec; $i++) {
    $o = $so + $i * 40
    $secs += ,([pscustomobject]@{
        VA = [BitConverter]::ToUInt32($b, $o + 12)
        VS = [BitConverter]::ToUInt32($b, $o + 8)
        RP = [BitConverter]::ToUInt32($b, $o + 20)
        RS = [BitConverter]::ToUInt32($b, $o + 16)
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

$slotRva = 0
$off = Rva2Off $impRva
while ($off -ge 0) {
    $nameRva = [BitConverter]::ToUInt32($b, $off + 12)
    if ($nameRva -eq 0) { break }
    $oft = [BitConverter]::ToUInt32($b, $off)
    $ft  = [BitConverter]::ToUInt32($b, $off + 16)
    $mod = ReadStr (Rva2Off $nameRva)
    $i = 0
    $t = if ($oft -ne 0) { $oft } else { $ft }
    while ($true) {
        $tv = [BitConverter]::ToUInt64($b, (Rva2Off $t) + $i * 8)
        if ($tv -eq 0) { break }
        if (($tv -band 0x8000000000000000) -eq 0) {
            $ibn = Rva2Off ([uint32]$tv)
            $fn = ReadStr ($ibn + 2)
            if ($fn -eq "CreateFileA" -and $mod -match "KERNEL32|api-ms-win-core-file") {
                $slotRva = [uint32]($ft + $i * 8)
            }
        }
        $i++
        if ($i -gt 2000) { break }
    }
    $off += 20
}
echo ("CreateFileA IAT slot RVA: 0x" + $slotRva.ToString("X"))
if ($slotRva -eq 0) { echo "IAT slot bulunamadi - devam"; }

# --- hedef ---
$z = Get-Process -Name zula -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $z) { echo "zula.exe yok"; exit 1 }
$pid2 = [uint32]$z.Id
echo ("hedef: zula pid=" + $pid2)

$dll = $b
if (Test-Path C:\zula\cheat.log) { Remove-Item C:\zula\cheat.log -Force }

[MMap]::Connect()
$va = [MMap]::Alloc($pid2, [uint64]$dll.Length, [uint32]0x40)
echo ("alloc: 0x" + $va.ToString("X"))
[MMap]::Write($pid2, $va, $dll)
$m = [MMap]::ManualMap($pid2, $va, [uint64]$dll.Length)
echo ("map: " + $m)
$baseHex  = ($m -split 'base=0x')[1].Split(' ')[0]
$entryHex = ($m -split 'entry=0x')[1].Split(' ')[0]
$base  = [Convert]::ToUInt64($baseHex, 16)
$entry = [Convert]::ToUInt64($entryHex, 16)

# --- FAZ 1: entry kodu yerinde mi? ---
$code = [MMap]::Read($pid2, $entry, 16)
echo ("entry kodu (16B): " + [MMap]::Hex($code))

# --- FAZ 2: IAT slot cozuldu mu? ---
if ($slotRva -ne 0) {
    $iat = [MMap]::Read($pid2, ($base + $slotRva), 8)
    $val = [BitConverter]::ToUInt64($iat, 0)
    echo ("IAT CreateFileA = 0x" + $val.ToString("X16") + "  " + $(if ($val -gt 0x7FF000000000) { "(user adresi - COZULDU)" } elseif ($val -eq 0) { "(BOS - COZULMEDI)" } else { "(supheli)" }))
}

# --- FAZ 3: thread ---
[MMap]::CreateThread($pid2, $entry, $base)
echo "thread olusturuldu - 6 sn gozlem"
for ($i = 0; $i -lt 6; $i++) {
    Start-Sleep -Seconds 1
    $alive = (Get-Process -Id $pid2 -ErrorAction SilentlyContinue) -ne $null
    $log = Test-Path C:\zula\cheat.log
    echo ("  t+" + ($i + 1) + "s zula=" + $alive + " cheat.log=" + $log)
    if ($log) { break }
}
echo "==== cheat.log:"
if (Test-Path C:\zula\cheat.log) { Get-Content C:\zula\cheat.log -Encoding UTF8 } else { echo "YOK" }
$alive = (Get-Process -Id $pid2 -ErrorAction SilentlyContinue) -ne $null
echo ("zula son durum: " + $(if ($alive) { "YASIYOR" } else { "OLDU" }))
