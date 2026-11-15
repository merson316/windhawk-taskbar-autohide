# Drives the taskbars on this PC and checks what they do, in place of testing
# by hand. It moves the pointer, presses Win and Win+A, and (only with -Edges)
# moves the taskbar to other edges, then puts the pointer and the edge back.
#
#   .\tools\test-taskbars.ps1 -Snapshot          look only: touches nothing
#   .\tools\test-taskbars.ps1                    the current edge
#   .\tools\test-taskbars.ps1 -Edges 3,0,1,2     each edge in turn (0 left,
#                                                1 top, 2 right, 3 bottom),
#                                                then back to where it began
#   .\tools\test-taskbars.ps1 -Out run.json      also write every result and
#                                                every animation trace
#   .\tools\test-taskbars.ps1 -Only reveal       scenarios whose name matches
#
# Keep your hands off the mouse while it runs. Moving the mouse stops the run
# and puts things back. It needs "Automatically hide the taskbar" on; it works
# with the mod enabled or disabled and says which it found (with the mod off,
# Windows hides the taskbar on the bottom edge only, and the hide and reveal
# checks are skipped elsewhere).
#
# What it checks, per edge and per taskbar:
#   edge      Windows reports the edge asked for, and still does 3 s later;
#             every taskbar sits on that edge of its own display
#   hide      with the pointer away each taskbar hides, and no part of it
#             shows on another display at any moment (issue #11)
#   reveal    the pointer at the display's edge brings it back whole, at its
#             display's DPI, in one movement: no step back, no jump, no stall
#   start     Win opens Start inside a display, the taskbar stays up while it
#             is open and hides after it closes
#   quick     Win+A opens Quick Settings inside a display and the taskbar
#             stays up while it is open (issue #10)
#
# What it cannot see: what is drawn inside the taskbar (icons, their size on
# screen, a transparent frame). It measures windows, not pixels.
#
# All coordinates are physical pixels of the virtual desktop.

param(
    [switch]$Snapshot,
    [string]$Edges = "",
    [string]$Only = "",
    [string]$Out = "",
    [int]$SettleMs = 3000
)

Add-Type @"
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public class TaskbarDriver {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct MONITORINFOEX {
        public int cbSize; public RECT rcMonitor; public RECT rcWork; public uint dwFlags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string szDevice;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct APPBARDATA {
        public int cbSize; public IntPtr hWnd; public uint uCallbackMessage; public uint uEdge;
        public RECT rc; public IntPtr lParam;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct MOUSEINPUT { public int dx, dy, mouseData, dwFlags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Sequential)]
    struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Explicit, Size = 40)]
    struct INPUT {
        [FieldOffset(0)] public int type;
        [FieldOffset(8)] public MOUSEINPUT mi;
        [FieldOffset(8)] public KEYBDINPUT ki;
    }
    delegate bool MonitorEnumProc(IntPtr hMonitor, IntPtr hdc, IntPtr rect, IntPtr data);

    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] static extern bool EnumDisplayMonitors(IntPtr hdc, IntPtr clip, MonitorEnumProc proc, IntPtr data);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool GetMonitorInfoW(IntPtr hMonitor, ref MONITORINFOEX info);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowTextW(IntPtr hWnd, StringBuilder text, int max);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] static extern int GetWindowRgnBox(IntPtr hWnd, out RECT box);
    [DllImport("user32.dll")] static extern int GetWindowLongW(IntPtr hWnd, int index);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool GetCursorPos(out POINT p);
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] static extern uint SendInput(uint count, INPUT[] inputs, int size);
    [DllImport("user32.dll")] static extern IntPtr SendMessageTimeoutW(IntPtr hWnd, uint msg, IntPtr w, IntPtr l, uint flags, uint ms, out IntPtr result);
    [DllImport("shcore.dll")] static extern int GetDpiForMonitor(IntPtr hMonitor, int type, out uint x, out uint y);
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr hWnd, int attr, out int value, int size);
    [DllImport("shell32.dll")] static extern UIntPtr SHAppBarMessage(uint msg, ref APPBARDATA data);
    [DllImport("winmm.dll")] static extern uint timeBeginPeriod(uint ms);
    [DllImport("winmm.dll")] static extern uint timeEndPeriod(uint ms);

    public class Monitor {
        public string Device; public bool Primary; public uint Dpi;
        public int Left, Top, Right, Bottom;
    }
    public class Bar {
        public string Name; public string Class; public long Hwnd; public bool Primary;
        public int Left, Top, Right, Bottom;          // the window
        public int Thickness;
        public string Home;                           // its display, "" if on no display's edge
        public uint Dpi; public uint HomeDpi;
        public int Showing;                           // thickness in view on its own display
        public long Spill;                            // pixels in view on other displays
        public int ContentDx, ContentDy;              // content window against the taskbar window
        public bool ContentFills; public bool ContentVisible;
        public bool Topmost; public bool Visible; public bool Cloaked;
        public string Region;                         // "" none, else its box in window coordinates
    }
    public class Panel {
        public string Class; public string Title; public long Hwnd;
        public int Left, Top, Right, Bottom; public bool Showing; public string Inside;
    }
    public class Sample {
        public double Ms; public int Showing; public long Spill; public int ContentDx, ContentDy;
        public uint Dpi; public bool ContentVisible;
    }

    static IntPtr Aware() { return SetThreadDpiAwarenessContext(new IntPtr(-4)); }
    static void Unaware(IntPtr old) { SetThreadDpiAwarenessContext(old); }

    static RECT Cut(RECT a, int l, int t, int r, int b) {
        RECT o = new RECT();
        o.Left = Math.Max(a.Left, l); o.Top = Math.Max(a.Top, t);
        o.Right = Math.Min(a.Right, r); o.Bottom = Math.Min(a.Bottom, b);
        if (o.Right < o.Left) o.Right = o.Left;
        if (o.Bottom < o.Top) o.Bottom = o.Top;
        return o;
    }
    static long Area(RECT a) { return (long)(a.Right - a.Left) * (a.Bottom - a.Top); }

    public static List<Monitor> Monitors() {
        IntPtr old = Aware();
        try {
            List<Monitor> list = new List<Monitor>();
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
                    m.Dpi = GetDpiForMonitor(h, 0, out x, out y) == 0 ? x : 0;
                    list.Add(m);
                }
                return true;
            }, IntPtr.Zero);
            return list;
        } finally { Unaware(old); }
    }

    public static uint Edge() {
        APPBARDATA pos = new APPBARDATA();
        pos.cbSize = Marshal.SizeOf(typeof(APPBARDATA));
        SHAppBarMessage(5, ref pos);                    // ABM_GETTASKBARPOS
        return pos.uEdge;
    }
    public static bool AutoHide() {
        APPBARDATA abd = new APPBARDATA();
        abd.cbSize = Marshal.SizeOf(typeof(APPBARDATA));
        return (SHAppBarMessage(4, ref abd).ToUInt64() & 1) != 0;   // ABM_GETSTATE
    }

    // The display a taskbar belongs to: the one whose edge it lies along,
    // shown or hidden, wherever most of the window happens to be.
    static Monitor HomeOf(RECT w, int th, uint edge, List<Monitor> mons) {
        foreach (Monitor m in mons) {
            bool at; long along; long span;
            if (edge == 3)      at = w.Top >= m.Bottom - th - 2 && w.Top <= m.Bottom + 2;
            else if (edge == 1) at = w.Bottom >= m.Top - 2 && w.Bottom <= m.Top + th + 2;
            else if (edge == 0) at = w.Right >= m.Left - 2 && w.Right <= m.Left + th + 2;
            else                at = w.Left >= m.Right - th - 2 && w.Left <= m.Right + 2;
            if (edge == 1 || edge == 3) {
                along = Math.Min(w.Right, m.Right) - Math.Max(w.Left, m.Left);
                span = Math.Min(w.Right - w.Left, m.Right - m.Left);
            } else {
                along = Math.Min(w.Bottom, m.Bottom) - Math.Max(w.Top, m.Top);
                span = Math.Min(w.Bottom - w.Top, m.Bottom - m.Top);
            }
            if (at && along * 2 > span) return m;
        }
        return null;
    }

    static Bar Measure(IntPtr h, string cls, uint edge, List<Monitor> mons) {
        Bar b = new Bar();
        RECT w; GetWindowRect(h, out w);
        b.Class = cls; b.Hwnd = h.ToInt64(); b.Primary = cls == "Shell_TrayWnd";
        b.Left = w.Left; b.Top = w.Top; b.Right = w.Right; b.Bottom = w.Bottom;
        b.Thickness = Math.Min(w.Right - w.Left, w.Bottom - w.Top);
        b.Dpi = GetDpiForWindow(h);
        b.Visible = IsWindowVisible(h);
        int cloaked = 0; DwmGetWindowAttribute(h, 14, out cloaked, 4);
        b.Cloaked = cloaked != 0;
        b.Topmost = (GetWindowLongW(h, -20) & 8) != 0;  // GWL_EXSTYLE, WS_EX_TOPMOST

        // What can be in view: the window, less whatever its region cuts off.
        RECT seen = w;
        RECT box;
        int kind = GetWindowRgnBox(h, out box);         // 0 none, 1 empty
        b.Region = kind == 0 ? "" : kind == 1 ? "empty" : box.Left + "," + box.Top + "," + box.Right + "," + box.Bottom;
        if (kind == 1) seen = Cut(w, 0, 0, 0, 0);
        else if (kind > 1) seen = Cut(w, w.Left + box.Left, w.Top + box.Top, w.Left + box.Right, w.Top + box.Bottom);
        if (!b.Visible || b.Cloaked) seen = Cut(w, 0, 0, 0, 0);

        Monitor home = HomeOf(w, b.Thickness, edge, mons);
        b.Home = home == null ? "" : home.Device;
        b.HomeDpi = home == null ? 0 : home.Dpi;
        foreach (Monitor m in mons) {
            RECT part = Cut(seen, m.Left, m.Top, m.Right, m.Bottom);
            if (m == home) b.Showing = (edge == 0 || edge == 2) ? part.Right - part.Left : part.Bottom - part.Top;
            else b.Spill += Area(part);
        }

        IntPtr content = FindWindowExW(h, IntPtr.Zero, "Windows.UI.Composition.DesktopWindowContentBridge", null);
        if (content != IntPtr.Zero) {
            RECT c; GetWindowRect(content, out c);
            b.ContentDx = c.Left - w.Left; b.ContentDy = c.Top - w.Top;
            b.ContentFills = c.Left == w.Left && c.Top == w.Top && c.Right == w.Right && c.Bottom == w.Bottom;
            b.ContentVisible = IsWindowVisible(content);
        }
        b.Name = b.Primary ? "primary" : "secondary@" + (b.Home == "" ? "?" : b.Home);
        return b;
    }

    public static List<Bar> Bars() {
        IntPtr old = Aware();
        try {
            List<Monitor> mons = Monitors();
            uint edge = Edge();
            List<Bar> list = new List<Bar>();
            foreach (string cls in new string[] { "Shell_TrayWnd", "Shell_SecondaryTrayWnd" }) {
                IntPtr w = IntPtr.Zero;
                while ((w = FindWindowExW(IntPtr.Zero, w, cls, null)) != IntPtr.Zero)
                    list.Add(Measure(w, cls, edge, mons));
            }
            return list;
        } finally { Unaware(old); }
    }

    // Watches one taskbar and records every change, until it has shown
    // `target` thickness for `stableMs`, or `maxMs` has passed.
    public static List<Sample> Trace(long hwnd, int target, int tolerance, int stableMs, int maxMs) {
        IntPtr old = Aware();
        try {
            List<Monitor> mons = Monitors();
            uint edge = Edge();
            IntPtr h = new IntPtr(hwnd);
            List<Sample> list = new List<Sample>();
            Stopwatch sw = Stopwatch.StartNew();
            double reached = -1;
            timeBeginPeriod(1);     // or Sleep(1) lasts 15 ms and steps are missed
            try {
            while (sw.Elapsed.TotalMilliseconds < maxMs) {
                Bar b = Measure(h, "", edge, mons);
                double now = sw.Elapsed.TotalMilliseconds;
                Sample last = list.Count > 0 ? list[list.Count - 1] : null;
                if (last == null || last.Showing != b.Showing || last.Spill != b.Spill ||
                    last.ContentDx != b.ContentDx || last.ContentDy != b.ContentDy ||
                    last.Dpi != b.Dpi || last.ContentVisible != b.ContentVisible) {
                    Sample s = new Sample();
                    s.Ms = now; s.Showing = b.Showing; s.Spill = b.Spill;
                    s.ContentDx = b.ContentDx; s.ContentDy = b.ContentDy;
                    s.Dpi = b.Dpi; s.ContentVisible = b.ContentVisible;
                    list.Add(s);
                }
                if (Math.Abs(b.Showing - target) <= tolerance) {
                    if (reached < 0) reached = now;
                    if (now - reached >= stableMs) break;
                } else reached = -1;
                Thread.Sleep(1);
            }
            } finally { timeEndPeriod(1); }
            return list;
        } finally { Unaware(old); }
    }

    static int lastX = int.MinValue, lastY = int.MinValue;

    public static void MoveTo(int x, int y) {
        IntPtr old = Aware();
        try {
            // One pixel short, then a real one-pixel move, so that Windows
            // sees mouse input and not only a new position.
            SetCursorPos(x - 1, y);
            INPUT[] inp = new INPUT[1];
            inp[0].type = 0; inp[0].mi.dx = 1; inp[0].mi.dwFlags = 1;   // MOUSEEVENTF_MOVE
            SendInput(1, inp, Marshal.SizeOf(typeof(INPUT)));
            Thread.Sleep(20);
            SetCursorPos(x, y);
            lastX = x; lastY = y;
        } finally { Unaware(old); }
    }
    public static int[] Pointer() {
        IntPtr old = Aware();
        try { POINT p; GetCursorPos(out p); return new int[] { p.X, p.Y }; }
        finally { Unaware(old); }
    }
    // The owner has taken the mouse back.
    public static bool PointerTaken() {
        if (lastX == int.MinValue) return false;
        int[] p = Pointer();
        return Math.Abs(p[0] - lastX) > 60 || Math.Abs(p[1] - lastY) > 60;
    }

    public static void Keys(ushort[] vks) {
        INPUT[] inp = new INPUT[vks.Length * 2];
        for (int i = 0; i < vks.Length; i++) {
            inp[i].type = 1; inp[i].ki.wVk = vks[i];
            int up = vks.Length * 2 - 1 - i;
            inp[up].type = 1; inp[up].ki.wVk = vks[i]; inp[up].ki.dwFlags = 2;   // KEYEVENTF_KEYUP
        }
        SendInput((uint)inp.Length, inp, Marshal.SizeOf(typeof(INPUT)));
    }

    // What Settings sends when the taskbar's position is changed: the tray's
    // private setting message, setting 6, value = the edge.
    public static bool SetEdge(uint edge) {
        IntPtr tray = FindWindowExW(IntPtr.Zero, IntPtr.Zero, "Shell_TrayWnd", null);
        if (tray == IntPtr.Zero) return false;
        IntPtr res;
        return SendMessageTimeoutW(tray, 0x5CA, new IntPtr(6), new IntPtr(edge), 2, 5000, out res) != IntPtr.Zero;
    }

    public static List<Panel> Panels(string cls, string title) {
        IntPtr old = Aware();
        try {
            List<Monitor> mons = Monitors();
            List<Panel> list = new List<Panel>();
            IntPtr w = IntPtr.Zero;
            while ((w = FindWindowExW(IntPtr.Zero, w, cls, title)) != IntPtr.Zero) {
                Panel p = new Panel();
                RECT r; GetWindowRect(w, out r);
                StringBuilder sb = new StringBuilder(128); GetWindowTextW(w, sb, 128);
                int cloaked = 0; DwmGetWindowAttribute(w, 14, out cloaked, 4);
                p.Class = cls; p.Title = sb.ToString(); p.Hwnd = w.ToInt64();
                p.Left = r.Left; p.Top = r.Top; p.Right = r.Right; p.Bottom = r.Bottom;
                p.Showing = IsWindowVisible(w) && cloaked == 0 && Area(r) > 0;
                p.Inside = "";
                foreach (Monitor m in mons)
                    if (r.Left >= m.Left && r.Top >= m.Top && r.Right <= m.Right && r.Bottom <= m.Bottom) p.Inside = m.Device;
                list.Add(p);
            }
            return list;
        } finally { Unaware(old); }
    }
}
"@

$EdgeName = @('left', 'top', 'right', 'bottom')
# A string, split here: powershell -File hands "3,0,1,2" over as one value.
$EdgeList = @($Edges -split '[ ,]+' | Where-Object { $_ -ne '' } | ForEach-Object { [int]$_ })
if ($EdgeList | Where-Object { $_ -lt 0 -or $_ -gt 3 }) { Write-Host "-Edges takes 0 to 3" -ForegroundColor Red; exit 2 }
$results = New-Object System.Collections.ArrayList
$traces = New-Object System.Collections.ArrayList

function Mod-Loaded {
    foreach ($p in Get-Process explorer -ErrorAction SilentlyContinue) {
        try { if ($p.Modules | Where-Object { $_.ModuleName -like '*taskbar-autohide-any-edge*' }) { return $true } } catch { }
    }
    return $false
}

function Describe($b) {
    "{0,-22} ({1},{2},{3},{4}) showing {5}/{6} px, {7} px on other displays, content at ({8},{9}){10}, dpi {11} (display {12}){13}" -f `
        $b.Name, $b.Left, $b.Top, $b.Right, $b.Bottom, $b.Showing, $b.Thickness, $b.Spill, $b.ContentDx, $b.ContentDy,
        $(if ($b.ContentVisible) { "" } else { " hidden" }), $b.Dpi, $b.HomeDpi,
        ($(if ($b.Topmost) { "" } else { ", NOT topmost" }) + $(if ($b.Region) { ", region $($b.Region)" } else { "" }) + $(if ($b.Cloaked) { ", cloaked" } else { "" }) + $(if ($b.Visible) { "" } else { ", not visible" }))
}

function Result($status, $name, $detail) {
    [void]$results.Add([ordered]@{ status = $status; name = $name; detail = $detail })
    $colour = @{ PASS = 'Green'; FAIL = 'Red'; SKIP = 'DarkYellow' }[$status]
    Write-Host ("{0}  {1}" -f $status, $name) -ForegroundColor $colour -NoNewline
    if ($detail) { Write-Host ("  -- " + $detail) } else { Write-Host "" }
}
function Check($name, $ok, $detail) { Result $(if ($ok) { 'PASS' } else { 'FAIL' }) $name $detail }
# Changes the edge the way Settings does: the stored value first (the Settings
# handler in every process watches it and repeats it to the taskbar whenever
# anything under that key changes), then the message itself. Sending only the
# message leaves the stored value behind, and the next repeat moves the
# taskbar back.
function Set-Edge($e) {
    Set-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced' -Name TaskbarLocation -Value ([int]$e) -Type DWord
    return [TaskbarDriver]::SetEdge([uint32]$e)
}

function Wanted($name) { -not $Only -or $name -match $Only }

function Guard {
    if ([TaskbarDriver]::PointerTaken()) { throw "the mouse was moved: stopping" }
}
function Wait($ms) {
    $until = [DateTime]::UtcNow.AddMilliseconds($ms)
    while ([DateTime]::UtcNow -lt $until) { Guard; Start-Sleep -Milliseconds 50 }
}
function Monitor-Named($device) { [TaskbarDriver]::Monitors() | Where-Object { $_.Device -eq $device } }

# A point no taskbar reacts to: the middle of the given display.
function Park($m) { [TaskbarDriver]::MoveTo([int](($m.Left + $m.Right) / 2), [int](($m.Top + $m.Bottom) / 2)) }

# The pixel at the middle of the display's edge the taskbar is on.
function Touch-Edge($m, $edge) {
    $cx = [int](($m.Left + $m.Right) / 2); $cy = [int](($m.Top + $m.Bottom) / 2)
    switch ($edge) {
        0 { [TaskbarDriver]::MoveTo($m.Left, $cy) }
        1 { [TaskbarDriver]::MoveTo($cx, $m.Top) }
        2 { [TaskbarDriver]::MoveTo($m.Right - 1, $cy) }
        3 { [TaskbarDriver]::MoveTo($cx, $m.Bottom - 1) }
    }
}

# Reads one movement out of a trace and says what is wrong with it.
function Judge-Trace($name, $trace, $from, $to, $homeDpi) {
    [void]$traces.Add([ordered]@{ name = $name; samples = $trace })
    $last = $trace[$trace.Count - 1]
    $faults = @()
    if ([Math]::Abs($last.Showing - $to) -gt 2) { $faults += "ended showing $($last.Showing) px, wanted $to" }
    $spill = ($trace | Measure-Object -Property Spill -Maximum).Maximum
    if ($spill -gt 0) { $faults += "$spill px showed on another display" }
    $wrongDpi = $trace | Where-Object { $homeDpi -and $_.Dpi -ne $homeDpi } | Select-Object -First 1
    if ($wrongDpi) { $faults += "window at dpi $($wrongDpi.Dpi), its display is $homeDpi" }

    # The movement: from the first change to the last.
    $moves = @($trace | Select-Object -Skip 1)
    $dir = [Math]::Sign($to - $from)
    $back = 0; $jump = 0; $prev = $trace[0]
    foreach ($s in $moves) {
        $d = ($s.Showing - $prev.Showing) * $dir
        if ($d -lt -1) { $back = [Math]::Max($back, -$d) }
        if ($d -gt $jump) { $jump = $d }
        $prev = $s
    }
    $steps = @($moves | Where-Object { $_.Showing -ne $from }).Count
    $ms = if ($moves.Count) { [int]($moves[$moves.Count - 1].Ms - $moves[0].Ms) } else { 0 }
    $dist = [Math]::Abs($to - $from)
    if ($back) { $faults += "went back $back px mid-way" }
    if ($dist -gt 8) {
        if ($jump -gt $dist * 0.6) { $faults += "jumped $jump of $dist px in one step (not animated)" }
        if ($ms -gt 800) { $faults += "took $ms ms" }
    }
    $summary = "$from->$($last.Showing) px in $ms ms, $steps steps, largest $jump px"
    if ($faults.Count) { Result 'FAIL' $name ($summary + ": " + ($faults -join "; ")) }
    else { Result 'PASS' $name $summary }
}

function Bar-Named($name) { [TaskbarDriver]::Bars() | Where-Object { $_.Name -eq $name } }

function Hidden-Target($b) { 0 }   # anything up to 2 px counts as hidden

function Test-Edge($edge, $changed) {
    $en = $EdgeName[$edge]
    $mons = [TaskbarDriver]::Monitors()
    $primary = $mons | Where-Object { $_.Primary }
    $mod = Mod-Loaded

    if (Wanted "edge") {
        $now = [TaskbarDriver]::Edge()
        Check "$en/edge/reported" ($now -eq $edge) "Windows reports $($EdgeName[$now])"
        if ($changed) {
            Wait 3000
            $now = [TaskbarDriver]::Edge()
            Check "$en/edge/stays" ($now -eq $edge) "3 s later Windows reports $($EdgeName[$now])"
        }
        foreach ($b in [TaskbarDriver]::Bars()) {
            Check "$en/edge/placed/$($b.Name)" ($b.Home -ne "") (Describe $b)
        }
    }
    if ([TaskbarDriver]::Edge() -ne $edge) { Result 'SKIP' "$en/hide, reveal, start, quick" "the taskbar is not on this edge"; return }

    $canHide = $mod -or $edge -eq 3
    if (-not $canHide) { Result 'SKIP' "$en/hide, reveal" "the mod is off, and Windows only hides on the bottom edge" }

    $names = @([TaskbarDriver]::Bars() | Where-Object { $_.Home -ne "" } | ForEach-Object { $_.Name })

    if ($canHide -and (Wanted "hide|reveal")) {
        foreach ($name in $names) {
            $b = Bar-Named $name
            if (-not $b) { Result 'FAIL' "$en/reveal/$name" "the taskbar is gone"; continue }
            $m = Monitor-Named $b.Home
            # A resting place on a display that is not this taskbar's, where
            # there is one, so that leaving is unmistakable.
            $away = $mons | Where-Object { $_.Device -ne $b.Home } | Select-Object -First 1
            if (-not $away) { $away = $m }

            Park $away
            $t = [TaskbarDriver]::Trace($b.Hwnd, 0, 2, 400, 6000)
            Guard
            $start = $t[0].Showing
            if (Wanted "hide") { Judge-Trace "$en/hide/$name (settle)" $t $start 0 $b.HomeDpi }

            Touch-Edge $m $edge
            $t = [TaskbarDriver]::Trace($b.Hwnd, $b.Thickness, 0, 400, 4000)
            Guard
            if (Wanted "reveal") {
                Judge-Trace "$en/reveal/$name" $t $t[0].Showing $b.Thickness $b.HomeDpi
                $shown = Bar-Named $name
                if ($shown) {
                    Check "$en/reveal/$name/content" ($shown.ContentFills -and $shown.ContentVisible) (Describe $shown)
                    Check "$en/reveal/$name/topmost" $shown.Topmost ""
                }
            }

            Park $away
            $t = [TaskbarDriver]::Trace($b.Hwnd, 0, 2, 400, 6000)
            Guard
            if (Wanted "hide") { Judge-Trace "$en/hide/$name" $t $t[0].Showing 0 $b.HomeDpi }
        }
    }

    $main = Bar-Named "primary"
    if ($main -and (Wanted "start")) {
        Park $primary
        Wait 1500
        [TaskbarDriver]::Keys([uint16[]]@(0x5B))          # Win
        Wait 1500
        $start = @([TaskbarDriver]::Panels("Windows.UI.Core.CoreWindow", "Start") | Where-Object { $_.Showing })
        if ($start.Count -eq 0) { Result 'SKIP' "$en/start" "no Start window found showing" }
        else {
            $p = $start[0]
            Check "$en/start/inside a display" ($p.Inside -ne "") ("Start at ({0},{1},{2},{3})" -f $p.Left, $p.Top, $p.Right, $p.Bottom)
            Wait 2000
            $b = Bar-Named "primary"
            Check "$en/start/taskbar stays up" ($b.Showing -eq $b.Thickness) (Describe $b)
        }
        [TaskbarDriver]::Keys([uint16[]]@(0x1B))          # Esc
        if ($canHide) {
            $t = [TaskbarDriver]::Trace($main.Hwnd, 0, 2, 400, 6000)
            Judge-Trace "$en/start/hides after" $t $t[0].Showing 0 $main.HomeDpi
        }
    }

    if ($main -and (Wanted "quick")) {
        Park $primary
        Wait 1000
        [TaskbarDriver]::Keys([uint16[]]@(0x5B, 0x41))    # Win+A
        Wait 1500
        $qs = @([TaskbarDriver]::Panels("ControlCenterWindow", $null) | Where-Object { $_.Showing })
        if ($qs.Count -eq 0) { Result 'SKIP' "$en/quick" "no Quick Settings window found showing" }
        else {
            $p = $qs[0]
            Check "$en/quick/inside a display" ($p.Inside -ne "") ("Quick Settings at ({0},{1},{2},{3})" -f $p.Left, $p.Top, $p.Right, $p.Bottom)
            Wait 3000
            $b = Bar-Named "primary"
            Check "$en/quick/taskbar stays up" ($b.Showing -eq $b.Thickness) (Describe $b)
        }
        [TaskbarDriver]::Keys([uint16[]]@(0x1B))
        Wait 500
        $still = @([TaskbarDriver]::Panels("ControlCenterWindow", $null) | Where-Object { $_.Showing })
        if ($still.Count) { [TaskbarDriver]::Keys([uint16[]]@(0x5B, 0x41)) }
        if ($canHide) {
            $t = [TaskbarDriver]::Trace($main.Hwnd, 0, 2, 400, 6000)
            Judge-Trace "$en/quick/hides after" $t $t[0].Showing 0 $main.HomeDpi
        }
    }
}

# ------------------------------------------------------------------ the run

$dll = (Get-Item "$env:WINDIR\System32\taskbar.dll").VersionInfo.FileVersionRaw.ToString()
$edge0 = [TaskbarDriver]::Edge()
"taskbar.dll {0}   mod loaded: {1}   auto-hide: {2}   edge: {3}" -f $dll, (Mod-Loaded), ([TaskbarDriver]::AutoHide()), $EdgeName[$edge0]
foreach ($m in [TaskbarDriver]::Monitors()) {
    "  {0,-10} ({1},{2},{3},{4}) dpi {5}{6}" -f $m.Device, $m.Left, $m.Top, $m.Right, $m.Bottom, $m.Dpi, $(if ($m.Primary) { "  primary" } else { "" })
}
foreach ($b in [TaskbarDriver]::Bars()) { "  " + (Describe $b) }
""
if ($Snapshot) { return }

if (-not [TaskbarDriver]::AutoHide()) {
    Write-Host "Windows reports auto-hide off. Turn on 'Automatically hide the taskbar' first." -ForegroundColor Red
    exit 2
}

$pointer0 = [TaskbarDriver]::Pointer()
$stopped = $null
try {
    if ($EdgeList.Count -eq 0) {
        Test-Edge $edge0 $false
    } else {
        foreach ($e in $EdgeList) {
            Guard
            $changed = [TaskbarDriver]::Edge() -ne $e
            if ($changed) {
                if (-not (Set-Edge $e)) { Result 'FAIL' "$($EdgeName[$e])/edge/asked" "the taskbar did not answer"; continue }
                Wait $SettleMs
            }
            Test-Edge $e $changed
        }
    }
} catch {
    $stopped = "$_"
    Write-Host "Stopped: $stopped" -ForegroundColor Red
} finally {
    if ([TaskbarDriver]::Edge() -ne $edge0) {
        [void](Set-Edge $edge0)
        Start-Sleep -Milliseconds 1500
        "edge put back to {0}: Windows reports {1}" -f $EdgeName[$edge0], $EdgeName[[TaskbarDriver]::Edge()]
    }
    if (-not $stopped) { [TaskbarDriver]::MoveTo($pointer0[0], $pointer0[1]) }
}

$fail = @($results | Where-Object { $_.status -eq 'FAIL' }).Count
$pass = @($results | Where-Object { $_.status -eq 'PASS' }).Count
$skip = @($results | Where-Object { $_.status -eq 'SKIP' }).Count
""
"{0} passed, {1} failed, {2} skipped{3}" -f $pass, $fail, $skip, $(if ($stopped) { " (run stopped early)" } else { "" })
if ($Out) {
    [ordered]@{
        time = (Get-Date).ToString('o'); taskbarDll = $dll; modLoaded = (Mod-Loaded)
        stopped = $stopped; results = $results; traces = $traces
    } | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 $Out
    "written to $Out"
}
if ($fail -or $stopped) { exit 1 }
