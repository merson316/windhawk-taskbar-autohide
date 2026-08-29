# Sample the foreground window's identity for 20 seconds, so a rule that
# separates taskbar flyouts from ordinary Explorer windows can be chosen from
# evidence rather than by excluding classes one bug at a time.
#
# From a PowerShell prompt in the repo root:
#
#   .\tools\fgwindow.ps1
#
# While it runs: open the tray overflow, close it, click a File Explorer
# window, then click the desktop. Then send the log it names.

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class Fg {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint c);
    [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int i);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int m);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    public static string Cls(IntPtr h) {
        var sb = new StringBuilder(256); GetClassName(h, sb, 256); return sb.ToString();
    }
}
"@

$out = Join-Path $env:LOCALAPPDATA "windhawk-autohide-fg.log"
"=== foreground window samples ===" | Out-File $out -Encoding utf8

$last = ""
for ($i = 0; $i -lt 200; $i++) {
    $h = [Fg]::GetForegroundWindow()
    if ($h -ne [IntPtr]::Zero) {
        $cls = [Fg]::Cls($h)
        $pid2 = 0; [void][Fg]::GetWindowThreadProcessId($h, [ref]$pid2)
        $proc = try { (Get-Process -Id $pid2 -ErrorAction Stop).ProcessName } catch { "?" }
        $owner = [Fg]::GetWindow($h, 4)          # GW_OWNER
        $ex = [Fg]::GetWindowLong($h, -20)       # GWL_EXSTYLE
        $tool = if ($ex -band 0x80) { "TOOLWINDOW" } else { "-" }
        $app  = if ($ex -band 0x40000) { "APPWINDOW" } else { "-" }
        $line = "{0,-34} pid {1,-6} {2,-14} owner {3,-12} ex 0x{4:X8} {5} {6}" -f `
                $cls, $pid2, $proc, $owner, $ex, $tool, $app
        if ($line -ne $last) { $line | Out-File $out -Append -Encoding utf8; $last = $line }
    }
    Start-Sleep -Milliseconds 100
}
"=== done ===" | Out-File $out -Append -Encoding utf8
Write-Host "Wrote $out"
