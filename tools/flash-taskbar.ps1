# Flash a taskbar button, which is what makes Windows force an auto-hidden
# taskbar visible. This is what a Discord/Teams style notification does.
#
# From a PowerShell prompt in the repo root:
#
#   .\tools\flash-taskbar.ps1
#   .\tools\flash-taskbar.ps1 -Process Discord
#
# FlashWindowEx does nothing while the target window is in the foreground, so
# the window is minimised first. That is why flashing a freshly launched app
# silently fails.

param(
    [string]$Process = "",
    [int]$Wait = 6,
    [int]$Count = 12
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Flasher {
    [StructLayout(LayoutKind.Sequential)]
    public struct FLASHWINFO {
        public uint cbSize; public IntPtr hwnd; public uint dwFlags;
        public uint uCount; public uint dwTimeout;
    }
    [DllImport("user32.dll")]
    public static extern bool FlashWindowEx(ref FLASHWINFO pwfi);
    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    public static void Minimize(IntPtr h) { ShowWindow(h, 6); }   // SW_MINIMIZE
    public static bool Flash(IntPtr h, uint count) {
        FLASHWINFO fi = new FLASHWINFO();
        fi.cbSize = (uint)Marshal.SizeOf(fi);
        fi.hwnd = h;
        fi.dwFlags = 0x2 | 0xC;   // FLASHW_TRAY | FLASHW_TIMERNOFG
        fi.uCount = count;
        fi.dwTimeout = 0;
        return FlashWindowEx(ref fi);
    }
}
"@

if ($Process) {
    $p = Get-Process -Name $Process -ErrorAction SilentlyContinue |
         Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if (-not $p) { Write-Host "No window found for process '$Process'."; exit 1 }
    Write-Host "Flashing existing window: $($p.ProcessName)"
} else {
    # Windows 11's Notepad is a packaged app: the process started here hands
    # over to another and exits, so its own window handle stays empty. Look
    # for the Notepad window that appears instead.
    $before = @(Get-Process -Name Notepad -ErrorAction SilentlyContinue | ForEach-Object { $_.Id })
    Start-Process notepad
    $p = $null
    for ($i = 0; $i -lt 50 -and -not $p; $i++) {
        Start-Sleep -Milliseconds 200
        $p = Get-Process -Name Notepad -ErrorAction SilentlyContinue |
             Where-Object { $_.MainWindowHandle -ne 0 -and $before -notcontains $_.Id } |
             Select-Object -First 1
    }
    if (-not $p) {
        # An already running Notepad may have taken the new window as a tab.
        $p = Get-Process -Name Notepad -ErrorAction SilentlyContinue |
             Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    }
    if (-not $p) { Write-Host "Notepad did not open a window."; exit 1 }
    Write-Host "Started Notepad."
}

# Must not be foreground, or the flash is silently ignored.
[Flasher]::Minimize($p.MainWindowHandle)
Start-Sleep -Milliseconds 500

Write-Host "Move the mouse away and let the taskbar hide. Flashing in $Wait s..."
Start-Sleep -Seconds $Wait

$ok = [Flasher]::Flash($p.MainWindowHandle, $Count)
Write-Host "FlashWindowEx returned $ok. The taskbar should appear once and hold."
