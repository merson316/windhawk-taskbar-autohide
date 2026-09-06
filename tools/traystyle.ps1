# Samples Shell_TrayWnd's extended style and z-order neighbours until stopped.
# Answers one question: while the taskbar is SHOWN, has it lost WS_EX_TOPMOST?
# The symptom is intermittent, so leave this running and read the log after you
# next see the taskbar behind something. Logs only on a change, so an untroubled
# session leaves a handful of lines.
# Usage:  .\tools\traystyle.ps1        (Ctrl+C to stop)
$ErrorActionPreference = 'Stop'

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr FindWindowW(string c, string n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern int GetWindowLongW(IntPtr h, int i);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr WindowFromPoint(POINT p);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetAncestor(IntPtr h, uint f);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int m);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int left, top, right, bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x, y; }
}
"@

function Describe([IntPtr]$h) {
  if ($h -eq [IntPtr]::Zero) { return '(none)' }
  $sb = New-Object System.Text.StringBuilder 256
  [void][W]::GetClassNameW($h, $sb, 256)
  $pid2 = 0; [void][W]::GetWindowThreadProcessId($h, [ref]$pid2)
  $name = try { (Get-Process -Id $pid2 -ErrorAction Stop).ProcessName } catch { '?' }
  $ex = [W]::GetWindowLongW($h, -20)
  $top = if ($ex -band 0x8) { ' TOPMOST' } else { '' }
  "$($sb.ToString()) [$name]$top"
}

$log = Join-Path $env:LOCALAPPDATA 'windhawk-autohide-zorder.log'
"--- started $(Get-Date -Format o) ---" | Out-File $log -Encoding utf8
Write-Host "Sampling until stopped (Ctrl+C). Logs only when the state changes."
Write-Host "Writing to $log`n"

$last = ''
while ($true) {
  $t = [W]::FindWindowW('Shell_TrayWnd', $null)
  if ($t -ne [IntPtr]::Zero) {
    $ex = [W]::GetWindowLongW($t, -20)
    $r = New-Object W+RECT
    [void][W]::GetWindowRect($t, [ref]$r)
    $p = New-Object W+POINT
    $p.x = [int](($r.left + $r.right) / 2)
    $p.y = [int](($r.top + $r.bottom) / 2)
    $at = [W]::WindowFromPoint($p)
    if ($at -ne [IntPtr]::Zero) { $at = [W]::GetAncestor($at, 2) }
    $covered = ($at -ne [IntPtr]::Zero -and $at -ne $t)
    $topmost = if ($ex -band 0x8) { 'TOPMOST' } else { 'NOT-TOPMOST' }
    $line = "exstyle=0x{0:X8} {1}  rect={2},{3}-{4},{5}  covered={6} by {7}" -f `
            $ex, $topmost, $r.left, $r.top, $r.right, $r.bottom, $covered, (Describe $at)
    if ($line -ne $last) {
      $last = $line
      $stamp = "[{0:HH:mm:ss.fff}] $line" -f (Get-Date)
      Write-Host $stamp
      $stamp | Out-File $log -Append -Encoding utf8
    }
  }
  Start-Sleep -Milliseconds 500
}
