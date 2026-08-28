# Flash a taskbar button, which is what makes Windows force an auto-hidden
# taskbar visible. Use this to test the hold-visible behaviour.
#
#   powershell -ExecutionPolicy Bypass -File tools\flash-taskbar.ps1
#
# Starts Notepad, waits so you can move the mouse away and let the taskbar
# hide, then flashes its button.

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
    public static void Flash(IntPtr h, uint count) {
        FLASHWINFO fi = new FLASHWINFO();
        fi.cbSize = (uint)Marshal.SizeOf(fi);
        fi.hwnd = h;
        fi.dwFlags = 0x2 | 0xC;   // FLASHW_TRAY | FLASHW_TIMERNOFG
        fi.uCount = count;
        fi.dwTimeout = 0;
        FlashWindowEx(ref fi);
    }
}
"@

$p = Start-Process notepad -PassThru
Start-Sleep -Seconds 2
Write-Host "Notepad started. Move the mouse away and let the taskbar hide."
Write-Host "Flashing in 6 seconds..."
Start-Sleep -Seconds 6
[Flasher]::Flash($p.MainWindowHandle, 10)
Write-Host "Flashed. The taskbar should appear once and hold, not strobe."
Write-Host "Close Notepad when done."
