# Measure where Windows puts each taskbar, and at which DPI, on this monitor
# layout. Read-only: it moves nothing and changes no setting.
#
# The question it answers (issue #11): when an auto-hidden taskbar sits on an
# edge with another display beyond it, does Windows park the hidden window
# mostly on that other display, and does the window then take that display's
# DPI? Run it with the mod disabled to see what Windows does on its own, then
# with the mod enabled to see what the mod changes.
#
# From a PowerShell prompt in the repo root:
#
#   .\tools\measure-taskbars.ps1                 one snapshot
#   .\tools\measure-taskbars.ps1 -Watch 30       30 s, a line whenever a taskbar
#                                                moves, changes DPI or visibility
#   .\tools\measure-taskbars.ps1 -Watch 30 -Out measure.jsonl
#
# For the experiment:
#   1. Settings > Personalisation > Taskbar > Taskbar behaviours: turn on
#      "Automatically hide the taskbar" and "Show my taskbar on all displays".
#   2. Settings > System > Display: arrange the displays (stacked, side by
#      side, offset) and give them different scaling.
#   3. Disable the mod in Windhawk. The first line of output says whether it
#      is still loaded in Explorer.
#   4. Run with -Watch 30, let every taskbar hide, then reveal each one once.
#
# All coordinates are physical pixels of the virtual desktop.

param(
    [int]$Watch = 0,
    [int]$IntervalMs = 50,
    [string]$Out = ""
)

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public class TaskbarMeasure {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct MONITORINFOEX {
        public int cbSize; public RECT rcMonitor; public RECT rcWork; public uint dwFlags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string szDevice;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct APPBARDATA {
        public int cbSize; public IntPtr hWnd; public uint uCallbackMessage; public uint uEdge;
        public RECT rc; public IntPtr lParam;
    }
    public delegate bool MonitorEnumProc(IntPtr hMonitor, IntPtr hdc, IntPtr rect, IntPtr data);

    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] static extern bool EnumDisplayMonitors(IntPtr hdc, IntPtr clip, MonitorEnumProc proc, IntPtr data);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool GetMonitorInfoW(IntPtr hMonitor, ref MONITORINFOEX info);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool GetCursorPos(out POINT p);
    [DllImport("shcore.dll")] static extern int GetDpiForMonitor(IntPtr hMonitor, int type, out uint x, out uint y);
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr hWnd, int attr, out int value, int size);
    [DllImport("shell32.dll")] static extern UIntPtr SHAppBarMessage(uint msg, ref APPBARDATA data);

    public class Monitor {
        public string Device; public bool Primary; public uint Dpi;
        public int Left, Top, Right, Bottom;
    }
    public class Taskbar {
        public string Class; public long Hwnd; public uint Dpi; public bool Visible; public bool Cloaked;
        public int Left, Top, Right, Bottom;
        // Pixels of the window on each monitor, as "device=count" in monitor order.
        public string Split; public string MostlyOn;
    }
    public class Snapshot {
        public List<Monitor> Monitors = new List<Monitor>();
        public List<Taskbar> Taskbars = new List<Taskbar>();
        public int CursorX, CursorY; public bool AutoHide;
    }

    static long Overlap(RECT a, Monitor m) {
        long w = Math.Min(a.Right, m.Right) - Math.Max(a.Left, m.Left);
        long h = Math.Min(a.Bottom, m.Bottom) - Math.Max(a.Top, m.Top);
        return (w > 0 && h > 0) ? w * h : 0;
    }

    public static Snapshot Take() {
        // Per-monitor aware v2 for this thread, so every rectangle is in
        // physical pixels whatever the PowerShell host declares.
        IntPtr old = SetThreadDpiAwarenessContext(new IntPtr(-4));
        try {
            Snapshot s = new Snapshot();
            EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, delegate(IntPtr h, IntPtr hdc, IntPtr r, IntPtr d) {
                MONITORINFOEX mi = new MONITORINFOEX();
                mi.cbSize = Marshal.SizeOf(typeof(MONITORINFOEX));
                if (GetMonitorInfoW(h, ref mi)) {
                    Monitor m = new Monitor();
                    m.Device = mi.szDevice.Replace("\\\\.\\", "");
                    m.Primary = (mi.dwFlags & 1) != 0;
                    m.Left = mi.rcMonitor.Left; m.Top = mi.rcMonitor.Top;
                    m.Right = mi.rcMonitor.Right; m.Bottom = mi.rcMonitor.Bottom;
                    uint x, y;
                    m.Dpi = GetDpiForMonitor(h, 0, out x, out y) == 0 ? x : 0;   // MDT_EFFECTIVE_DPI
                    s.Monitors.Add(m);
                }
                return true;
            }, IntPtr.Zero);

            foreach (string cls in new string[] { "Shell_TrayWnd", "Shell_SecondaryTrayWnd" }) {
                IntPtr w = IntPtr.Zero;
                while ((w = FindWindowExW(IntPtr.Zero, w, cls, null)) != IntPtr.Zero) {
                    Taskbar t = new Taskbar();
                    RECT r; GetWindowRect(w, out r);
                    t.Class = cls; t.Hwnd = w.ToInt64(); t.Dpi = GetDpiForWindow(w);
                    t.Visible = IsWindowVisible(w);
                    int cloaked = 0;
                    DwmGetWindowAttribute(w, 14, out cloaked, 4);   // DWMWA_CLOAKED
                    t.Cloaked = cloaked != 0;
                    t.Left = r.Left; t.Top = r.Top; t.Right = r.Right; t.Bottom = r.Bottom;
                    StringBuilder split = new StringBuilder();
                    long best = 0;
                    t.MostlyOn = "(no monitor)";
                    foreach (Monitor m in s.Monitors) {
                        long px = Overlap(r, m);
                        if (px == 0) continue;
                        if (split.Length > 0) split.Append(' ');
                        split.Append(m.Device).Append('=').Append(px);
                        if (px > best) { best = px; t.MostlyOn = m.Device; }
                    }
                    t.Split = split.ToString();
                    s.Taskbars.Add(t);
                }
            }

            POINT p; GetCursorPos(out p);
            s.CursorX = p.X; s.CursorY = p.Y;
            APPBARDATA abd = new APPBARDATA();
            abd.cbSize = Marshal.SizeOf(typeof(APPBARDATA));
            s.AutoHide = (SHAppBarMessage(4, ref abd).ToUInt64() & 1) != 0;   // ABM_GETSTATE, ABS_AUTOHIDE
            return s;
        } finally {
            SetThreadDpiAwarenessContext(old);
        }
    }
}
"@

function Format-Rect($o) { "({0},{1},{2},{3}) {4}x{5}" -f $o.Left, $o.Top, $o.Right, $o.Bottom, ($o.Right - $o.Left), ($o.Bottom - $o.Top) }

function Format-Taskbar($t) {
    $state = if (-not $t.Visible) { "not visible" } elseif ($t.Cloaked) { "cloaked" } else { "visible" }
    "{0,-22} hwnd 0x{1:X}  {2}  dpi {3} ({4}%)  {5}  mostly on {6}  [{7}]" -f `
        $t.Class, $t.Hwnd, (Format-Rect $t), $t.Dpi, [int]($t.Dpi * 100 / 96), $state, $t.MostlyOn, $t.Split
}

function Get-Context {
    $adv = Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced' -ErrorAction SilentlyContinue
    $mod = $false
    foreach ($p in Get-Process explorer -ErrorAction SilentlyContinue) {
        try {
            if ($p.Modules | Where-Object { $_.ModuleName -like '*taskbar-autohide-any-edge*' }) { $mod = $true }
        } catch { }
    }
    [ordered]@{
        time        = (Get-Date).ToString('o')
        taskbarDll  = (Get-Item "$env:WINDIR\System32\taskbar.dll").VersionInfo.FileVersionRaw.ToString()
        modLoaded   = $mod
        allDisplays = [int]$adv.MMTaskbarEnabled
        alignment   = [int]$adv.TaskbarAl
    }
}

$ctx = Get-Context
$first = [TaskbarMeasure]::Take()

"taskbar.dll {0}   mod loaded in Explorer: {1}   auto-hide: {2}   taskbar on all displays: {3}" -f `
    $ctx.taskbarDll, $ctx.modLoaded, $first.AutoHide, ($ctx.allDisplays -eq 1)
""
"Monitors:"
foreach ($m in $first.Monitors) {
    "  {0,-10} {1}  dpi {2} ({3}%){4}" -f $m.Device, (Format-Rect $m), $m.Dpi, [int]($m.Dpi * 100 / 96), $(if ($m.Primary) { "  primary" } else { "" })
}
""
"Taskbars:"
foreach ($t in $first.Taskbars) { "  " + (Format-Taskbar $t) }
if (-not $first.Taskbars) { "  (none found)" }

function Write-Record($record) {
    if ($Out) { ($record | ConvertTo-Json -Depth 6 -Compress) | Add-Content -Path $Out -Encoding UTF8 }
}
Write-Record ([ordered]@{ kind = 'snapshot'; context = $ctx; monitors = $first.Monitors; taskbars = $first.Taskbars
                          cursor = @($first.CursorX, $first.CursorY); autoHide = $first.AutoHide })

if ($Watch -le 0) { return }

""
"Watching for $Watch s. Let each taskbar hide, then reveal it. A line appears on every change."
$last = @{}
foreach ($t in $first.Taskbars) { $last[$t.Hwnd] = Format-Taskbar $t }
$start = [Diagnostics.Stopwatch]::StartNew()
while ($start.Elapsed.TotalSeconds -lt $Watch) {
    Start-Sleep -Milliseconds $IntervalMs
    $s = [TaskbarMeasure]::Take()
    foreach ($t in $s.Taskbars) {
        $line = Format-Taskbar $t
        if ($last[$t.Hwnd] -ne $line) {
            $last[$t.Hwnd] = $line
            "{0,7:N2}s  cursor ({1},{2})  {3}" -f $start.Elapsed.TotalSeconds, $s.CursorX, $s.CursorY, $line
            Write-Record ([ordered]@{ kind = 'change'; t = [math]::Round($start.Elapsed.TotalSeconds, 3)
                                      cursor = @($s.CursorX, $s.CursorY); taskbar = $t })
        }
    }
}
"Done."
