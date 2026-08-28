// ==WindhawkMod==
// @id              taskbar-autohide-any-edge
// @name            Taskbar auto-hide on any edge
// @description     Makes "Automatically hide the taskbar" work with the taskbar docked to the top, left or right
// @version         1.3.0
// @author          merson316
// @github          https://github.com/merson316
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lcomctl32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar auto-hide on any edge

Windows 11 26H2 can dock the taskbar to the top, left or right, but
"Automatically hide the taskbar" only works on the bottom edge. Elsewhere the
setting is accepted, reports success, and does nothing.

`TrayUI::GetAutoHideFlags` masks the auto-hide flags to zero unless the taskbar
is docked to the bottom. The flags are stored correctly on every edge; only the
read is masked, and everything downstream is gated on it, so no hide is ever
attempted. This mod unmasks the read, stores the same flag combination the
bottom edge uses, and supplies the pointer tracking that Windows only performs
on the bottom edge.

The bottom edge is left untouched.

## Notification handling

Controlled by the two "Notifications" settings. **Set "keep visible for" to 0 to
turn it off entirely**; any value above 0 turns it on.

Windows refuses to auto-hide the taskbar while an app is flashing its taskbar
button, which can leave it visible indefinitely. This mod shows it once, holds
it for `notificationHoldMs` (1500 ms by default), then lets it hide and stay
hidden for the rest of the flashing.

This works on every edge, including the bottom, and is independent of the
edge the taskbar is docked to, including the bottom.

That makes Cirn09's [Better Taskbar
Autohide](https://windhawk.net/mods/taskbar-autohide-better) redundant; run one
or the other, not both, since they hook the same function.

## Known limitations

- No smooth show/hide animation: the Windows 11 easing is a XAML compositor
  animation that is never triggered off-bottom.
- Primary taskbar only. Secondary taskbars use a separate `CSecondaryTray`
  class.
- An edge with another display beyond it has nowhere off-screen to hide into,
  so the mod skips it and that edge behaves as it does without the mod. This is
  detected automatically; see "Skip edges facing another display".
- Requires Windows 11 build 26200 or newer.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- skipAdjacentEdges: true
  $name: Skip edges facing another display
  $description: >-
    An edge with a monitor beyond it has nowhere to hide into: the hidden
    position is occupied screen space on the neighbour, so the taskbar would
    slide into view there rather than out of sight. Such an edge is left to
    behave as it does without this mod.
- hideDurationMs: 0
  $name: Hide animation duration (ms)
  $description: >-
    How long the taskbar takes to slide out of view. 0 uses 400 ms; range
    25-2000.

    The mod animates the hide itself: Windows clamps each intermediate position
    to the endpoint, which is why an unmodified vertical taskbar appears to
    vanish instantly.
- showDurationMs: 0
  $name: Reveal animation duration (ms)
  $description: 0 keeps the Windows default of 200 ms. Range 25-2000.
- notificationHoldMs: 1500
  $name: Notifications - keep visible for (ms)
  $description: >-
    Windows will not auto-hide the taskbar while an app is flashing its taskbar
    button, which can leave it on screen indefinitely. Instead, the taskbar
    shows once, stays for this long, then hides for the rest of the flashing.

    Set to 0 to turn notification handling off. Applies on every edge,
    including the bottom.
- notificationBurstGapMs: 5000
  $name: Notifications - treat as one within (ms)
  $description: >-
    A flashing button asks for the taskbar once per flash, not once per
    notification. Requests closer together than this count as one, so the
    taskbar shows once instead of repeatedly.

    Raise it if the taskbar keeps reappearing during a single notification;
    lower it if two separate notifications get merged. Unused when the setting
    above is 0.
- revealDwellMs: 120
  $name: Reveal dwell (ms)
  $description: >-
    How long the pointer must stay in the trigger strip before the taskbar
    appears. Raise it if the taskbar appears when you were only moving to the
    next screen; 0 reveals instantly.
- revealTolerance: 8
  $name: Reveal trigger width (pixels)
  $description: >-
    How close to the screen edge the pointer must get to bring the taskbar
    back. Raise it if the taskbar is hard to summon, lower it if it appears
    when you did not mean it to.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <shellapi.h>
#include <string>

#include <utility>

// ABE_LEFT/TOP/RIGHT/BOTTOM and ABS_AUTOHIDE/ABS_ALWAYSONTOP come from
// shellapi.h.

// Member offsets within TrayUI, relative to the base used by SetAutoHideFlags,
// GetDockedRect, Unhide and SlideWindow. GetAutoHideFlags is reached on a base
// 8 bytes lower, so it sees these at 0x118 and 0x44. Both are validated at load
// against a byte signature inside GetAutoHideFlags.
#define TRAYUI_EDGE_OFFSET  0x110
#define TRAYUI_FLAGS_OFFSET 0x3c
#define GETFLAGS_EDGE_OFFSET  0x118
#define GETFLAGS_FLAGS_OFFSET 0x44

// WM_TIMER is only delivered when the thread's queue is empty, so hovering the
// taskbar -- which produces a stream of mouse messages -- starves it exactly
// when the reveal is needed. A worker thread posts an ordinary message instead,
// which is not deprioritised; the work still runs on the taskbar's thread.
#define WATCH_TICK_MS 40

UINT g_watchMsg = 0;
HANDLE g_watchThread = nullptr;
volatile LONG g_watchStop = 0;
#define LEAVE_TICKS    2

// ------------------------------------------------------------------ settings

struct Settings {
    bool skipAdjacentEdges = true;
    int revealTolerance = 8;
    int revealDwellMs = 120;
    int notificationHoldMs = 1500;
    int notificationBurstGapMs = 5000;
    int hideDurationMs = 0;
    int showDurationMs = 0;
} g_settings;

int ClampDuration(int ms) {
    if (ms <= 0) {
        return 0;   // 0 means "use the Windows default"
    }
    if (ms < 25) {
        return 25;
    }
    if (ms > 2000) {
        return 2000;
    }
    return ms;
}

void LoadSettings() {
    g_settings.skipAdjacentEdges =
        Wh_GetIntSetting(L"skipAdjacentEdges") != 0;
    g_settings.revealTolerance = Wh_GetIntSetting(L"revealTolerance");
    if (g_settings.revealTolerance < 1) {
        g_settings.revealTolerance = 1;
    } else if (g_settings.revealTolerance > 64) {
        g_settings.revealTolerance = 64;
    }

    // Clamp out-of-range values rather than discarding them: silently turning
    // an over-large value into "no override" looks identical to the setting
    // being ignored.
    g_settings.notificationHoldMs = Wh_GetIntSetting(L"notificationHoldMs");
    if (g_settings.notificationHoldMs < 0) {
        g_settings.notificationHoldMs = 0;
    } else if (g_settings.notificationHoldMs > 30000) {
        g_settings.notificationHoldMs = 30000;
    }

    g_settings.revealDwellMs = Wh_GetIntSetting(L"revealDwellMs");
    if (g_settings.revealDwellMs < 0) {
        g_settings.revealDwellMs = 0;
    } else if (g_settings.revealDwellMs > 2000) {
        g_settings.revealDwellMs = 2000;
    }

    g_settings.notificationBurstGapMs =
        Wh_GetIntSetting(L"notificationBurstGapMs");
    if (g_settings.notificationBurstGapMs < 0) {
        g_settings.notificationBurstGapMs = 0;
    } else if (g_settings.notificationBurstGapMs > 60000) {
        g_settings.notificationBurstGapMs = 60000;
    }

    g_settings.hideDurationMs = ClampDuration(Wh_GetIntSetting(L"hideDurationMs"));
    g_settings.showDurationMs = ClampDuration(Wh_GetIntSetting(L"showDurationMs"));
}


extern HWND g_trayWnd;   // defined with the taskbar state below

// True when another display sits immediately beyond this edge of the taskbar's
// own monitor. Such an edge has nowhere to hide into -- the hidden position is
// occupied screen space on the neighbour -- so the mod leaves it alone.
bool DisplayBeyondEdge(UINT edge) {
    if (!g_trayWnd) {
        return false;
    }

    HMONITOR own = MonitorFromWindow(g_trayWnd, MONITOR_DEFAULTTONULL);
    MONITORINFO mi{sizeof(mi)};
    if (!own || !GetMonitorInfoW(own, &mi)) {
        return false;
    }

    const RECT& m = mi.rcMonitor;
    POINT probe{};
    switch (edge) {
        case ABE_LEFT:  probe = {m.left - 1, (m.top + m.bottom) / 2}; break;
        case ABE_RIGHT: probe = {m.right, (m.top + m.bottom) / 2};    break;
        case ABE_TOP:   probe = {(m.left + m.right) / 2, m.top - 1};  break;
        default:        return false;
    }

    HMONITOR there = MonitorFromPoint(probe, MONITOR_DEFAULTTONULL);
    return there && there != own;
}

// EdgeEnabled sits in the hook path, which runs far too often to query the
// display layout on every call. A second of staleness is harmless: the answer
// changes only when monitors are rearranged.
volatile LONG g_facingEdge = -1;
volatile LONG g_facingResult = 0;
volatile LONG g_facingTick = 0;

bool EdgeFacesDisplay(UINT edge) {
    const LONG now = (LONG)GetTickCount64();
    const ULONG since =
        (ULONG)(now - InterlockedCompareExchange(&g_facingTick, 0, 0));
    if (InterlockedCompareExchange(&g_facingEdge, -1, -1) != (LONG)edge ||
        since > 1000) {
        const LONG r = DisplayBeyondEdge(edge) ? 1 : 0;
        InterlockedExchange(&g_facingResult, r);
        InterlockedExchange(&g_facingTick, now);
        InterlockedExchange(&g_facingEdge, (LONG)edge);
        return r != 0;
    }

    return InterlockedCompareExchange(&g_facingResult, 0, 0) != 0;
}

bool EdgeEnabled(UINT edge) {
    if (edge >= ABE_BOTTOM) {
        return false;   // bottom works natively
    }

    return !(g_settings.skipAdjacentEdges && EdgeFacesDisplay(edge));
}


// ---------------------------------------------------------------- build gate

typedef struct _MOD_OSVERSIONINFOW {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
} MOD_OSVERSIONINFOW;

ULONG g_buildNumber = 0;

bool IsSupportedBuild() {
    using RtlGetVersion_t = LONG(WINAPI*)(MOD_OSVERSIONINFOW*);

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return false;
    }

    auto pRtlGetVersion =
        (RtlGetVersion_t)(void*)GetProcAddress(ntdll, "RtlGetVersion");
    if (!pRtlGetVersion) {
        return false;
    }

    MOD_OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (pRtlGetVersion(&vi) != 0) {
        return false;
    }

    g_buildNumber = vi.dwBuildNumber;
    return vi.dwBuildNumber >= 26200;
}

// ------------------------------------------------------------ hooked symbols

using TrayUI_GetStuckPlace_t = UINT(WINAPI*)(void* pThis);
using TrayUI_GetAutoHideFlags_t = UINT(WINAPI*)(void* pThis);
using TrayUI_SetAutoHideFlags_t = void(WINAPI*)(void* pThis, UINT flags);
using TrayUI_GetDockedRect_t = UINT(WINAPI*)(void* pThis, RECT* rect, int arg);
using TrayUI_Unhide_t = void(WINAPI*)(void* pThis, int flags, int request);
using TrayUI_SlideWindow_t = void(WINAPI*)(void* pThis, HWND hWnd,
                                           const RECT* rect, HMONITOR monitor,
                                           bool show, bool animate);

TrayUI_GetStuckPlace_t TrayUI_GetStuckPlace_Original;
TrayUI_GetAutoHideFlags_t TrayUI_GetAutoHideFlags_Original;
TrayUI_SetAutoHideFlags_t TrayUI_SetAutoHideFlags_Original;
TrayUI_GetDockedRect_t TrayUI_GetDockedRect_Original;
TrayUI_Unhide_t TrayUI_Unhide_Original;
// Resolved, never hooked. Registering the same symbol twice in one HookSymbols
// call corrupts the trampoline and breaks every slide, so the address is used
// directly for both the signature check and the calls.
TrayUI_SlideWindow_t TrayUI_SlideWindow_Original;

bool g_offsetsVerified = false;

// A TrayUI instance on the base the calls above expect, captured from
// GetDockedRect.
void* g_trayUI = nullptr;

HWND g_trayWnd = nullptr;
bool g_subclassed = false;
int g_leaveTicks = 0;
ULONGLONG g_dwellStart = 0;

// Windows unhides the taskbar itself for notifications and flashing buttons.
// Re-hiding immediately makes it flap, so hold it visible for a while after any
// unhide the mod did not issue.
volatile LONG64 g_holdVisibleUntil = 0;
volatile LONG64 g_lastNotifyUnhide = 0;

// A flashing taskbar button produces one unhide per flash. Unhides closer
// together than notificationBurstGapMs belong to the same burst and are treated
// as one notification, so the taskbar shows once, hides after the hold, and
// stays hidden while the flashing continues.
RECT g_lastDockedRect{};

// Set while Windows is unhiding, and while the watcher is calling in.
thread_local bool g_inUnhide = false;
thread_local bool g_inWatcherCall = false;

UINT CurrentStuckPlace(void* pThis) {
    if (!TrayUI_GetStuckPlace_Original || !pThis) {
        return 0xFFFFFFFF;
    }
    return TrayUI_GetStuckPlace_Original(pThis);
}

// ------------------------------------------------------------ pointer helper

RECT g_monitorRect{};
ULONGLONG g_monitorRectAt = 0;

bool TrayMonitorRect(RECT* out) {
    ULONGLONG now = GetTickCount64();
    if (g_monitorRect.right == 0 || now - g_monitorRectAt > 2000) {
        HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (!tray) {
            return false;
        }
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(MonitorFromWindow(tray, MONITOR_DEFAULTTOPRIMARY),
                             &mi)) {
            return false;
        }
        g_monitorRect = mi.rcMonitor;
        g_monitorRectAt = now;
    }
    *out = g_monitorRect;
    return true;
}

// True when the pointer is within the thin strip that triggers a reveal. This
// is not the same as being over the taskbar: once revealed, the taskbar
// occupies its full width and the stay-visible test uses its docked rect.
bool CursorAtRevealStrip(UINT edge) {
    POINT pt;
    if (!GetCursorPos(&pt)) {
        return false;
    }

    RECT m{};
    if (!TrayMonitorRect(&m)) {
        return false;
    }

    if (pt.x < m.left || pt.x > m.right || pt.y < m.top || pt.y > m.bottom) {
        return false;
    }

    const LONG tol = g_settings.revealTolerance;
    switch (edge) {
        case ABE_LEFT:  return pt.x <= m.left + tol;
        case ABE_TOP:   return pt.y <= m.top + tol;
        case ABE_RIGHT: return pt.x >= m.right - tol - 1;
        case ABE_BOTTOM: return pt.y >= m.bottom - tol - 1;
        default:        return false;
    }
}

// The pointer being over whatever remains of the hidden taskbar should reveal
// it too, independently of the trigger width.
bool CursorOverTaskbar(void* pThis) {
    if (!pThis || !TrayUI_GetDockedRect_Original) {
        return false;
    }
    RECT r{};
    TrayUI_GetDockedRect_Original(pThis, &r, 0);
    POINT pt;
    return GetCursorPos(&pt) && PtInRect(&r, pt);
}

// ------------------------------------------------------------------- hooks

// Returns the stored flags rather than the zero the original reports when the
// taskbar is not docked to the bottom.
UINT WINAPI TrayUI_GetAutoHideFlags_Hook(void* pThis) {
    UINT ret = TrayUI_GetAutoHideFlags_Original(pThis);
    if (!pThis || !g_offsetsVerified) {
        return ret;
    }

    UINT edge = *(UINT*)((BYTE*)pThis + GETFLAGS_EDGE_OFFSET);
    if (edge >= ABE_BOTTOM || !EdgeEnabled(edge)) {
        return ret;
    }

    // This function is reached on a base 8 bytes below the one the other calls
    // use, so it is a second source for the pointer the watcher needs. Without
    // it the watcher idles until a layout pass happens to run GetDockedRect.
    if (!g_trayUI) {
        g_trayUI = (BYTE*)pThis + 8;
    }

    return *(UINT*)((BYTE*)pThis + GETFLAGS_FLAGS_OFFSET);
}

// GetDockedRect returns the hidden rect only when both ABS_AUTOHIDE and
// ABS_ALWAYSONTOP are set. Windows adds the second bit itself on the bottom
// edge but not elsewhere, so add it here. Not while unhiding, where a bare
// ABS_AUTOHIDE means "show".
void WINAPI TrayUI_SetAutoHideFlags_Hook(void* pThis, UINT flags) {
    UINT edge = CurrentStuckPlace(pThis);
    UINT effective = flags;

    if ((flags & ABS_AUTOHIDE) && edge < ABE_BOTTOM && EdgeEnabled(edge) &&
        !g_inUnhide && !CursorAtRevealStrip(edge)) {
        effective = flags | ABS_ALWAYSONTOP;
    }

    TrayUI_SetAutoHideFlags_Original(pThis, effective);
}

UINT WINAPI TrayUI_GetDockedRect_Hook(void* pThis, RECT* rect, int arg) {
    g_trayUI = pThis;
    return TrayUI_GetDockedRect_Original(pThis, rect, arg);
}

// Request 9 is Windows' "auto-hide state changed, ensure shown" reflex. It
// fires as soon as the hidden state is set and would undo the hide, so drop it
// unless the pointer is actually at the edge.
#define UNHIDE_REQUEST_STATE_CHANGED 9

void WINAPI TrayUI_Unhide_Hook(void* pThis, int flags, int request) {
    UINT edge = CurrentStuckPlace(pThis);

    if (request == UNHIDE_REQUEST_STATE_CHANGED && edge < ABE_BOTTOM &&
        EdgeEnabled(edge) && !g_inWatcherCall && !CursorAtRevealStrip(edge)) {
        return;
    }

    if (!g_inWatcherCall && g_settings.notificationHoldMs > 0) {
        LONG64 now = (LONG64)GetTickCount64();
        LONG64 last = InterlockedExchange64(&g_lastNotifyUnhide, now);

        if (now - last > g_settings.notificationBurstGapMs) {
            // A new notification: show it for the configured hold.
            InterlockedExchange64(&g_holdVisibleUntil,
                                  now + g_settings.notificationHoldMs);
        } else if (now >= InterlockedCompareExchange64(&g_holdVisibleUntil, 0,
                                                       0) &&
                   edge <= ABE_BOTTOM && !CursorAtRevealStrip(edge)) {
            // Same burst, already shown for its hold, and the pointer is not at
            // the edge: keep it hidden rather than flashing it back up. Applies
            // on every edge including the bottom, where Windows drives the
            // hiding itself.
            return;
        }
    }

    bool prev = g_inUnhide;
    g_inUnhide = true;
    TrayUI_Unhide_Original(pThis, flags, request);
    g_inUnhide = prev;
}

// -------------------------------------------------- slide animation duration

// SlideWindow animates with a synchronous timed loop, taking its duration from
// this+0x168 when hiding and this+0x16c when showing (400 ms and 200 ms by
// default). The animation itself works correctly off-bottom -- measured step
// counts, spacing and flags match the bottom edge exactly -- but a vertical
// taskbar sweeps far less screen area for the same travel, so it reads as much
// faster. These offsets let that be tuned.
//
// Verified against the two load instructions inside SlideWindow, which encode
// both offsets, so a changed function disables the feature rather than writing
// to arbitrary memory.
#define SLIDE_HIDE_DURATION_OFFSET 0x168
#define SLIDE_SHOW_DURATION_OFFSET 0x16c

const size_t kShowDurSigOffset = 0xA6;
const BYTE kShowDurSig[] = {0x44, 0x8B, 0xB3, 0x6C, 0x01, 0x00, 0x00};
const size_t kHideDurSigOffset = 0x123;
const BYTE kHideDurSig[] = {0x44, 0x8B, 0xB3, 0x68, 0x01, 0x00, 0x00};

bool g_durationOffsetsVerified = false;

// Captured before the first override so the Windows defaults can be put back
// when a setting returns to 0. Without this the member keeps the last value
// written and no setting can clear it.
bool g_defaultDurationsCaptured = false;
int g_defaultHideDuration = 0;
int g_defaultShowDuration = 0;

void ApplyDurationOverrides(void* pThis) {
    if (!g_durationOffsetsVerified || !pThis) {
        return;
    }

    int* hide = (int*)((BYTE*)pThis + SLIDE_HIDE_DURATION_OFFSET);
    int* show = (int*)((BYTE*)pThis + SLIDE_SHOW_DURATION_OFFSET);

    if (!g_defaultDurationsCaptured) {
        g_defaultHideDuration = *hide;
        g_defaultShowDuration = *show;
        g_defaultDurationsCaptured = true;
        Wh_Log(L"captured default slide durations hide=%d show=%d",
               g_defaultHideDuration, g_defaultShowDuration);
    }

    *hide = g_settings.hideDurationMs > 0 ? g_settings.hideDurationMs
                                          : g_defaultHideDuration;
    *show = g_settings.showDurationMs > 0 ? g_settings.showDurationMs
                                          : g_defaultShowDuration;
}

// ------------------------------------------------- notification auto-hide

// Windows refuses to auto-hide while an app flashes its taskbar button, and
// keeps refusing until the flashing stops, so the taskbar can sit there
// indefinitely. CTaskListWnd::PermitAutoHide is the veto, and it is on Windows'
// own hide path -- the path that runs on the bottom edge, where this mod's
// watcher is deliberately inactive.
//
// Overriding it here gives the same behaviour on every edge from one mod. The
// approach is the one used by Cirn09's "Better Taskbar Autohide"
// (https://windhawk.net/mods/taskbar-autohide-better), reimplemented against
// this mod's own notification tracking rather than a separate message hook.

using CTaskListWnd_PermitAutoHide_t = int(WINAPI*)(void* pThis);
CTaskListWnd_PermitAutoHide_t CTaskListWnd_PermitAutoHide_Original;
int WINAPI CTaskListWnd_PermitAutoHide_Hook(void* pThis) {
    if (g_settings.notificationHoldMs > 0 &&
        (LONG64)GetTickCount64() >=
            InterlockedCompareExchange64(&g_holdVisibleUntil, 0, 0)) {
        return TRUE;   // hold elapsed: allow hiding even if Windows objects
    }
    return CTaskListWnd_PermitAutoHide_Original(pThis);
}

// ------------------------------------------------------------- hide animation

// The taskbar rewrites requested window positions in its WM_WINDOWPOSCHANGING
// handler, forcing every intermediate position to the endpoint the auto-hide
// flags imply. That is why the hide appeared instant: the window jumped to the
// hidden rect on the first step of the slide and never occupied the positions
// in between.
//
// Animating here means two things: move the window from the watch thread rather
// than through SlideWindow, which runs its loop on the taskbar's UI thread and
// blocks the repaint; and restore the requested position after the clamp, so
// intermediate frames survive.

volatile LONG g_animRequested = 0;
volatile LONG g_animOverride = 0;
RECT g_animFrom{}, g_animTo{};
int g_animDurationMs = 400;
UINT g_animEdge = ABE_LEFT;

// True when another display occupies the space the taskbar would hide into.
// Sliding a right-docked taskbar off a 3440-wide primary puts it at x >= 3440,
// which is the next monitor, so it would hide onto that screen rather than out
// of sight. Tested by asking which monitor the far edge lands on.
bool HiddenRectSpillsOntoAnotherDisplay(const RECT& hidden, UINT edge) {
    if (!g_trayWnd) {
        return false;
    }

    HMONITOR own = MonitorFromWindow(g_trayWnd, MONITOR_DEFAULTTOPRIMARY);
    POINT probe{};
    const LONG midX = (hidden.left + hidden.right) / 2;
    const LONG midY = (hidden.top + hidden.bottom) / 2;
    switch (edge) {
        case ABE_LEFT:  probe.x = hidden.left;      probe.y = midY; break;
        case ABE_RIGHT: probe.x = hidden.right - 1; probe.y = midY; break;
        case ABE_TOP:   probe.x = midX; probe.y = hidden.top;        break;
        default:        probe.x = midX; probe.y = hidden.bottom - 1; break;
    }

    HMONITOR there = MonitorFromPoint(probe, MONITOR_DEFAULTTONULL);
    return there != nullptr && there != own;
}

// Set while the taskbar is parked as a clipped sliver, so the clamp can be held
// against Windows reasserting its own hidden rect.
volatile LONG g_clipHold = 0;
RECT g_clipRect{};

// Set when the taskbar was hidden with ShowWindow because there was no
// off-screen space to slide into.
volatile LONG g_windowHidden = 0;
RECT g_dockedBeforeHide{};

void RunHideAnimation() {
    const RECT from = g_animFrom, to = g_animTo;
    const int ms = g_animDurationMs > 0 ? g_animDurationMs : 400;

    InterlockedExchange(&g_animOverride, 1);

    // Reachable only with "Skip edges facing another display" turned off: the
    // hiding direction runs into another display, so there is nowhere to slide.
    RECT mon{};
    const bool haveMon = TrayMonitorRect(&mon);
    const bool spills = HiddenRectSpillsOntoAnotherDisplay(to, g_animEdge);
    const bool clip = haveMon && spills;


    if (clip) {
        // Nowhere to slide to: every position beyond this edge is on the
        // neighbouring display, and the taskbar's content renders past any clip
        // we apply, so animating would just play the slide over there. Hide it
        // outright instead.
        InterlockedExchange(&g_clipHold, 0);
        InterlockedExchange(&g_windowHidden, 1);
        ShowWindow(g_trayWnd, SW_HIDE);
        InterlockedExchange(&g_animOverride, 0);
        return;
    }


    const ULONGLONG start = GetTickCount64();
    for (;;) {
        // Abandon the slide if the mod is unloading: this runs on the watch
        // thread, and a full-length animation would outlast the join in
        // StopWatchThread, leaving the thread inside a module being unloaded.
        if (InterlockedCompareExchange(&g_watchStop, 0, 0)) {
            break;
        }

        const ULONGLONG elapsed = GetTickCount64() - start;
        if (elapsed >= (ULONGLONG)ms) {
            break;
        }
        const double t = (double)elapsed / ms;
        LONG x = from.left + (LONG)((to.left - from.left) * t);
        LONG y = from.top + (LONG)((to.top - from.top) * t);

        SetWindowPos(g_trayWnd, nullptr, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        Sleep(16);
    }

    SetWindowPos(g_trayWnd, nullptr, to.left, to.top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    InterlockedExchange(&g_animOverride, 0);
}

// ------------------------------------------------------------ pointer watcher

// Windows' cursor-at-edge detection does not run off-bottom, and none of the
// hooked functions are called while the taskbar sits idle, so there is no
// polling site. A timer on the taskbar's own thread supplies one.
void WatcherTick() {
    // No pointer yet: nothing has run GetDockedRect and no state change has run
    // GetAutoHideFlags. Force a recompute rather than waiting for the user to
    // interact with something. SWP_FRAMECHANGED provokes the layout that
    // WM_SETTINGCHANGE alone did not.
    if (!g_trayUI && g_trayWnd) {
        static int nudgeTicks = 0;
        if ((nudgeTicks++ % 8) == 0) {
            SetWindowPos(g_trayWnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                             SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    }

    if (!g_trayUI || !g_offsetsVerified || g_inWatcherCall ||
        !TrayUI_Unhide_Original || !TrayUI_SetAutoHideFlags_Original ||
        !TrayUI_GetDockedRect_Original) {
        return;
    }

    UINT edge = *(UINT*)((BYTE*)g_trayUI + TRAYUI_EDGE_OFFSET);
    if (edge >= ABE_BOTTOM || !EdgeEnabled(edge)) {
        return;
    }

    UINT stored = *(UINT*)((BYTE*)g_trayUI + TRAYUI_FLAGS_OFFSET);
    if (!(stored & ABS_AUTOHIDE)) {
        return;
    }

    if (stored & ABS_ALWAYSONTOP) {
        // Hidden. Reveal once the pointer reaches the trigger strip; Unhide
        // performs the slide itself.
        if (!CursorAtRevealStrip(edge) && !CursorOverTaskbar(g_trayUI)) {
            g_leaveTicks = 0;
            return;
        }

        ApplyDurationOverrides(g_trayUI);
        InterlockedExchange(&g_clipHold, 0);

        if (InterlockedCompareExchange(&g_windowHidden, 0, 1) == 1) {
            // Was hidden outright rather than moved: put it back before asking
            // Windows to unhide, so it reappears at full size in place.
            SetWindowPos(g_trayWnd, nullptr, g_dockedBeforeHide.left,
                         g_dockedBeforeHide.top,
                         g_dockedBeforeHide.right - g_dockedBeforeHide.left,
                         g_dockedBeforeHide.bottom - g_dockedBeforeHide.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(g_trayWnd, SW_SHOWNA);
        }

        g_inWatcherCall = true;
        TrayUI_Unhide_Original(g_trayUI, 2, 1);
        g_inWatcherCall = false;
        g_leaveTicks = 0;
        return;
    }

    // Shown. Stay up while the pointer is over the taskbar, or while the
    // taskbar is foreground, which covers Start and Search being open.
    RECT docked{};
    TrayUI_GetDockedRect_Original(g_trayUI, &docked, 0);

    POINT pt{};
    if (!GetCursorPos(&pt)) {
        return;
    }

    g_lastDockedRect = docked;   // remembered so unload can restore it

    if (PtInRect(&docked, pt) || GetForegroundWindow() == g_trayWnd) {
        g_leaveTicks = 0;
        return;
    }

    if ((LONG64)GetTickCount64() <
        InterlockedCompareExchange64(&g_holdVisibleUntil, 0, 0)) {
        g_leaveTicks = 0;
        return;   // Windows asked for it to be visible
    }

    if (++g_leaveTicks < LEAVE_TICKS) {
        return;
    }
    g_leaveTicks = 0;

    // Unhide slides itself, but nothing performs the hide slide off-bottom, so
    // set the state and then issue the slide. With the flags now set,
    // GetDockedRect returns the hidden rect.
    g_inWatcherCall = true;
    TrayUI_SetAutoHideFlags_Original(g_trayUI, stored | ABS_ALWAYSONTOP);

    if (g_trayWnd) {
        RECT hidden{};
        TrayUI_GetDockedRect_Original(g_trayUI, &hidden, 0);

        GetWindowRect(g_trayWnd, &g_animFrom);
        g_dockedBeforeHide = docked;
        g_animTo = hidden;
        g_animDurationMs =
            g_settings.hideDurationMs > 0 ? g_settings.hideDurationMs : 400;
        g_animEdge = edge;
        InterlockedExchange(&g_animRequested, 1);
    }
    g_inWatcherCall = false;
}

LRESULT CALLBACK TrayWatchProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                               LPARAM lParam, DWORD_PTR dwRefData) {
    if (uMsg == WM_WINDOWPOSCHANGING && lParam &&
        InterlockedCompareExchange(&g_clipHold, 0, 0) &&
        !InterlockedCompareExchange(&g_animOverride, 0, 0)) {
        // Parked as a clipped sliver: Windows would otherwise put the window
        // back at its own hidden rect, which is on the neighbouring display.
        WINDOWPOS* wp = (WINDOWPOS*)lParam;
        LRESULT r = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        wp->x = g_clipRect.left;
        wp->y = g_clipRect.top;
        wp->cx = g_clipRect.right - g_clipRect.left;
        wp->cy = g_clipRect.bottom - g_clipRect.top;
        wp->flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
        return r;
    }

    if (uMsg == WM_WINDOWPOSCHANGING && lParam &&
        InterlockedCompareExchange(&g_animOverride, 0, 0)) {
        WINDOWPOS* wp = (WINDOWPOS*)lParam;
        const int reqX = wp->x, reqY = wp->y;

        LRESULT r = DefSubclassProc(hWnd, uMsg, wParam, lParam);

        // Put back what we asked for: the taskbar clamps every intermediate
        // position to the endpoint, which is what made the slide invisible.
        wp->x = reqX;
        wp->y = reqY;
        return r;
    }

    if (g_watchMsg && uMsg == g_watchMsg) {
        WatcherTick();
        return 0;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

DWORD WINAPI WatchThreadProc(LPVOID) {
    while (!InterlockedCompareExchange(&g_watchStop, 0, 0)) {
        if (InterlockedCompareExchange(&g_animRequested, 0, 1) == 1) {
            RunHideAnimation();
        }

        if (g_trayWnd && g_watchMsg) {
            PostMessageW(g_trayWnd, g_watchMsg, 0, 0);
        }
        Sleep(WATCH_TICK_MS);
    }
    return 0;
}

// ------------------------------------------------------------------ lifecycle

// The two member offsets are read out of GetAutoHideFlags, so confirm the
// function still matches the form they were taken from before trusting them.
// The sequence encodes both.
const size_t kSigOffset = 0x19;
const BYTE kSig[] = {
    0x83, 0xBB, 0x18, 0x01, 0x00, 0x00, 0x03,  // cmpl $0x3,0x118(%rbx)
    0x74, 0x04,                                // je   .return_real
    0x33, 0xC0,                                // xor  %eax,%eax
    0xEB, 0x03,                                // jmp
    0x8B, 0x43, 0x44,                          // mov  0x44(%rbx),%eax
};

void Wh_ModSettingsChanged() {
    LoadSettings();
}

BOOL Wh_ModInit() {
    if (!IsSupportedBuild()) {
        Wh_Log(L"unsupported build %u, mod inactive", g_buildNumber);
        return FALSE;
    }

    LoadSettings();

    HMODULE taskbar =
        LoadLibraryExW(L"taskbar.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!taskbar) {
        Wh_Log(L"could not load taskbar.dll");
        return FALSE;
    }

    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{LR"(public: virtual unsigned int __cdecl TrayUI::GetStuckPlace(void))",
          LR"(public: virtual unsigned int __cdecl TrayUI::GetStuckPlace(void) )"},
         &TrayUI_GetStuckPlace_Original, nullptr, true},

        {{LR"(public: virtual unsigned int __cdecl TrayUI::GetAutoHideFlags(void))",
          LR"(public: virtual unsigned int __cdecl TrayUI::GetAutoHideFlags(void) )"},
         &TrayUI_GetAutoHideFlags_Original, TrayUI_GetAutoHideFlags_Hook, true},

        {{LR"(public: virtual void __cdecl TrayUI::SetAutoHideFlags(unsigned int))",
          LR"(public: virtual void __cdecl TrayUI::SetAutoHideFlags(unsigned int) )"},
         &TrayUI_SetAutoHideFlags_Original, TrayUI_SetAutoHideFlags_Hook, true},

        {{LR"(public: virtual unsigned int __cdecl TrayUI::GetDockedRect(struct tagRECT *,int))",
          LR"(public: virtual unsigned int __cdecl TrayUI::GetDockedRect(struct tagRECT *,int) )"},
         &TrayUI_GetDockedRect_Original, TrayUI_GetDockedRect_Hook, true},

        {{LR"(public: virtual void __cdecl TrayUI::Unhide(enum TrayCommon::TrayUnhideFlags,enum TrayCommon::UnhideRequest))",
          LR"(public: virtual void __cdecl TrayUI::Unhide(enum TrayCommon::TrayUnhideFlags,enum TrayCommon::UnhideRequest) )"},
         &TrayUI_Unhide_Original, TrayUI_Unhide_Hook, true},

        {{LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool))",
          LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool) )"},
         &TrayUI_SlideWindow_Original, nullptr, true},

        {{LR"(public: virtual int __cdecl CTaskListWnd::PermitAutoHide(void))",
          LR"(public: virtual int __cdecl CTaskListWnd::PermitAutoHide(void) )"},
         &CTaskListWnd_PermitAutoHide_Original,
         CTaskListWnd_PermitAutoHide_Hook, true},
    };

    if (!WindhawkUtils::HookSymbols(taskbar, hooks, ARRAYSIZE(hooks))) {
        Wh_Log(L"HookSymbols failed");
        return FALSE;
    }

    // Every one of these is load-bearing: running with any missing would change
    // nothing while appearing to work.
    struct {
        PCWSTR name;
        void* ptr;
    } required[] = {
        {L"GetStuckPlace", (void*)TrayUI_GetStuckPlace_Original},
        {L"GetAutoHideFlags", (void*)TrayUI_GetAutoHideFlags_Original},
        {L"SetAutoHideFlags", (void*)TrayUI_SetAutoHideFlags_Original},
        {L"GetDockedRect", (void*)TrayUI_GetDockedRect_Original},
        {L"Unhide", (void*)TrayUI_Unhide_Original},
        {L"SlideWindow", (void*)TrayUI_SlideWindow_Original},
    };

    for (const auto& r : required) {
        if (!r.ptr) {
            Wh_Log(L"required symbol %s unresolved, mod inactive", r.name);
            return FALSE;
        }
    }

    const BYTE* slide = (const BYTE*)TrayUI_SlideWindow_Original;
    g_durationOffsetsVerified =
        memcmp(slide + kShowDurSigOffset, kShowDurSig, sizeof(kShowDurSig)) ==
            0 &&
        memcmp(slide + kHideDurSigOffset, kHideDurSig, sizeof(kHideDurSig)) == 0;
    if (!g_durationOffsetsVerified) {
        Wh_Log(L"slide duration offsets unverified, durations left alone");
    } else {
        Wh_Log(L"slide duration offsets verified");
    }

    const BYTE* sig = (const BYTE*)TrayUI_GetAutoHideFlags_Original + kSigOffset;
    g_offsetsVerified = memcmp(sig, kSig, sizeof(kSig)) == 0;
    if (!g_offsetsVerified) {
        Wh_Log(L"GetAutoHideFlags does not match the expected form, "
               L"member offsets are not trustworthy, mod inactive");
        return FALSE;
    }

    // Subclass here rather than from a hook: this runs off the taskbar's
    // thread, which is what SetWindowSubclassFromAnyThread expects.
    g_trayWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (g_trayWnd) {
        g_subclassed = WindhawkUtils::SetWindowSubclassFromAnyThread(
                           g_trayWnd, TrayWatchProc, 0) != FALSE;
    }
    if (!g_subclassed) {
        Wh_Log(L"could not subclass Shell_TrayWnd, reveal will not work");
    }

    g_watchMsg = RegisterWindowMessageW(L"WindhawkTaskbarAutoHideAnyEdgeTick");
    if (!g_watchMsg) {
        Wh_Log(L"could not register the tick message");
        return FALSE;
    }

    g_watchThread = CreateThread(nullptr, 0, WatchThreadProc, nullptr, 0,
                                 nullptr);
    if (!g_watchThread) {
        Wh_Log(L"could not start the watch thread");
        return FALSE;
    }

    return TRUE;
}

// Unloading while the taskbar is hidden would strand it off-screen: Windows
// stops believing auto-hide is active, so nothing brings it back. Restore it
// before the hooks go away, using only plain Win32 from whichever thread this
// runs on -- calling TrayUI methods cross-thread is what crashed an earlier
// build.
void RestoreTaskbarIfHidden() {
    if (!g_trayUI || !g_offsetsVerified || !g_trayWnd) {
        return;
    }

    UINT edge = *(UINT*)((BYTE*)g_trayUI + TRAYUI_EDGE_OFFSET);
    if (edge >= ABE_BOTTOM) {
        return;
    }

    UINT* flags = (UINT*)((BYTE*)g_trayUI + TRAYUI_FLAGS_OFFSET);
    if (!(*flags & ABS_AUTOHIDE) || !(*flags & ABS_ALWAYSONTOP)) {
        return;   // not hidden
    }

    *flags &= ~ABS_ALWAYSONTOP;   // no longer "hidden"

    if (InterlockedCompareExchange(&g_windowHidden, 0, 1) == 1) {
        ShowWindow(g_trayWnd, SW_SHOWNA);
    }

    if (g_lastDockedRect.right > g_lastDockedRect.left) {
        SetWindowPos(g_trayWnd, nullptr, g_lastDockedRect.left,
                     g_lastDockedRect.top,
                     g_lastDockedRect.right - g_lastDockedRect.left,
                     g_lastDockedRect.bottom - g_lastDockedRect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    PostMessageW(g_trayWnd, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
}

void StopWatchThread() {
    InterlockedExchange(&g_watchStop, 1);
    if (g_watchThread) {
        WaitForSingleObject(g_watchThread, 2000);
        CloseHandle(g_watchThread);
        g_watchThread = nullptr;
    }
}

void Wh_ModBeforeUninit() {
    // Stop the watcher first: left running, it can hide the taskbar again
    // between the restore below and the thread being stopped, which strands it
    // off-screen -- exactly what the restore exists to prevent.
    StopWatchThread();
    RestoreTaskbarIfHidden();
}

void Wh_ModUninit() {
    StopWatchThread();
    if (g_trayWnd && g_subclassed) {
        WindhawkUtils::RemoveWindowSubclassFromAnyThread(g_trayWnd,
                                                         TrayWatchProc);
    }
}
