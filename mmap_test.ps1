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
}
'@

Add-Type -TypeDefinition $src -ErrorAction Stop

$z = Get-Process -Name zula -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $z) { echo "zula.exe yok - once oyunu baslat"; exit 1 }
$pid2 = [uint32]$z.Id
echo ("hedef: zula pid=" + $pid2)

$dll = [IO.File]::ReadAllBytes("C:\zula\cheat.dll")
echo ("dll boyut: " + $dll.Length)

if (Test-Path C:\zula\cheat.log) { Remove-Item C:\zula\cheat.log -Force }

[MMap]::Connect()
$va = [MMap]::Alloc($pid2, [uint64]$dll.Length, [uint32]0x40)
echo ("alloc: 0x" + $va.ToString("X"))
[MMap]::Write($pid2, $va, $dll)
echo "yazildi"
$m = [MMap]::ManualMap($pid2, $va, [uint64]$dll.Length)
echo ("map: " + $m)
$entryHex = ($m -split 'entry=0x')[1].Split(' ')[0]
$baseHex  = ($m -split 'base=0x')[1].Split(' ')[0]
[MMap]::CreateThread($pid2, [Convert]::ToUInt64($entryHex,16), [Convert]::ToUInt64($baseHex,16))
echo "thread olusturuldu"

Start-Sleep -Seconds 4
echo "==== cheat.log:"
if (Test-Path C:\zula\cheat.log) { Get-Content C:\zula\cheat.log -Encoding UTF8 } else { echo "YOK" }
