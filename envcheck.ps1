$src = @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class DQ5 {
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern uint QueryDosDevice(string name, IntPtr target, int max);
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern uint QueryDosDevice(string name, StringBuilder target, int max);

  public static string[] List() {
    IntPtr buf = Marshal.AllocHGlobal(1048576);
    try {
      uint r = QueryDosDevice(null, buf, 1048576);
      if (r == 0) return new string[] { "ERR " + Marshal.GetLastWin32Error() };
      var res = new System.Collections.Generic.List<string>();
      IntPtr p = buf;
      while (true) {
        string s = Marshal.PtrToStringUni(p);
        if (string.IsNullOrEmpty(s)) break;
        res.Add(s);
        p = IntPtr.Add(p, (s.Length + 1) * 2);
      }
      return res.ToArray();
    } finally { Marshal.FreeHGlobal(buf); }
  }
}
'@
if (-not ('DQ5' -as [type])) { Add-Type -TypeDefinition $src -ErrorAction SilentlyContinue }

Write-Output ("session=" + (Get-Process -Id $PID).SessionId)
Write-Output ("admin=" + (New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))

foreach ($n in @('zuladrv', 'C:', 'NUL')) {
    $sb = New-Object System.Text.StringBuilder 2048
    $r = [DQ5]::QueryDosDevice($n, $sb, 2048)
    if ($r -ne 0) {
        Write-Output ("QD " + $n + " -> " + (($sb.ToString() -split "`0")[0]))
    } else {
        Write-Output ("QD " + $n + " -> YOK err=" + [Runtime.InteropServices.Marshal]::GetLastWin32Error())
    }
}

$items = [DQ5]::List()
Write-Output ("LISTE toplam=" + $items.Count)
$hit = @($items | Where-Object { $_ -match 'zula' })
if ($hit.Count -gt 0) { Write-Output ("LISTE HIT " + ($hit -join ',')) } else { Write-Output 'LISTE zula YOK' }
Write-Output ("LISTE ilk10=" + (($items | Select-Object -First 10) -join ','))
