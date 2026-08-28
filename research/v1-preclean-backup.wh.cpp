// ==WindhawkMod==
// @id              taskbar-autohide-any-edge
// @name            Taskbar auto-hide on any edge
// @description     Makes "Automatically hide the taskbar" work with the taskbar docked to the top, left or right
// @version         1.0.0
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
setting is accepted, reports success, and does nothing at all.

This mod makes it work on the other three edges.

## What was actually wrong

`TrayUI::GetAutoHideFlags` masks the auto-hide flags to zero unless the taskbar
is docked to the bottom:

    cmpl $0x3,0x118(%rbx)     ; edge == ABE_BOTTOM ?
    je   .return_real
    xor  %eax,%eax            ; otherwise report no flags
    ret
  .return_real:
    mov  0x44(%rbx),%eax      ; the genuinely stored flags

The flags are stored correctly on every edge. Everything downstream is gated on
the masked result -- GetDockedRect returns the hidden rect only when
(flags & 3) == 3 -- so no hide is ever attempted.

## What the mod does

- Reports the genuinely stored flags instead of the masked zero.
- Stores ABS_ALWAYSONTOP alongside ABS_AUTOHIDE off-bottom, which is what the
  bottom edge does and what the hidden-rect test requires.
- Watches the pointer on a timer and drives reveal and hide, because Windows'
  own cursor-at-edge detection does not run off-bottom.
- Leaves the bottom edge completely untouched.

## Known limitations

- No smooth show/hide animation. The Windows 11 easing is a XAML compositor
  animation that is never triggered off-bottom, so the taskbar moves without it.
- Primary taskbar only. Secondary taskbars use a separate CSecondaryTray class
  and are not handled.
- Tied to Taskbar.dll internals. The member offsets are validated against a byte
  signature inside GetAutoHideFlags; if a Windows update changes it, the mod
  goes inert rather than misbehaving.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- edgeLeft: true
  $name: Enable on the left edge
- edgeTop: true
  $name: Enable on the top edge
- edgeRight: true
  $name: Enable on the right edge
- revealTolerance: 4
  $name: Reveal trigger width (pixels)
  $description: >-
    How close to the screen edge the pointer must get before the taskbar
    reappears.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <stdarg.h>
#include <string>

// ---------------------------------------------------------------- build gate

// Declared locally so the mod does not depend on winternl.h being present.
typedef struct _DIAG_OSVERSIONINFOW {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
} DIAG_OSVERSIONINFOW;

ULONG g_buildNumber = 0;

bool IsSupportedBuild() {
    using RtlGetVersion_t = LONG(WINAPI*)(DIAG_OSVERSIONINFOW*);

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return false;
    }

    auto pRtlGetVersion =
        (RtlGetVersion_t)(void*)GetProcAddress(ntdll, "RtlGetVersion");
    if (!pRtlGetVersion) {
        return false;
    }

    DIAG_OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (pRtlGetVersion(&vi) != 0) {
        return false;
    }

    g_buildNumber = vi.dwBuildNumber;
    return vi.dwBuildNumber >= 26200;
}

// ------------------------------------------------------------------- logging

// Forward to Windhawk's logging, which is gated on the user enabling it. The
// investigation build wrote its own file to %LOCALAPPDATA%; a shipped mod
// should not.
#define DiagLog(fmt, ...) Wh_Log(fmt, ##__VA_ARGS__)

PCWSTR EdgeName(UINT edge) {
    switch (edge) {
        case 0: return L"LEFT";
        case 1: return L"TOP";
        case 2: return L"RIGHT";
        case 3: return L"BOTTOM";
        default: return L"?(bad this)";
    }
}

#define ABE_BOTTOM 3

// Flags Windows asked to store, and the live edge. Tracked globally because
// the getter arrives through an adjustor thunk, so its `this` cannot be
// matched against the setter's.
volatile LONG g_desiredFlags = 0;
volatile LONG g_currentEdge = ABE_BOTTOM;

// Set only for the duration of a SetAutoHideState call on this thread, so the
// edge check inside it passes. The gate lives in SetAutoHideState, not in the
// flag store: on the bottom edge the entire hide (flags 0x3, hidden-rect
// computation and slide) happens inside one SetAutoHideState bracket, and on
// other edges everything after the first SetAutoHideFlags is skipped.
thread_local bool g_pretendBottom = false;

// The tracked edge is only corrected once SetAutoHideFlags or SetStuckPlace
// runs, but SetAutoHideState -- which contains the gate -- is the OUTER call.
// Without seeding, the first and decisive SetAutoHideState sees the default and
// is not spoofed. Derive the edge from the reserved work area instead, which
// needs no window messaging and so cannot re-enter the shell.
UINT DetectEdgeFromWorkArea() {
    RECT wa{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) {
        return 0xFFFFFFFF;
    }

    LONG sw = GetSystemMetrics(SM_CXSCREEN);
    LONG sh = GetSystemMetrics(SM_CYSCREEN);

    LONG insetLeft = wa.left;
    LONG insetTop = wa.top;
    LONG insetRight = sw - wa.right;
    LONG insetBottom = sh - wa.bottom;

    LONG best = insetLeft;
    UINT edge = ABE_LEFT;
    if (insetTop > best) { best = insetTop; edge = ABE_TOP; }
    if (insetRight > best) { best = insetRight; edge = ABE_RIGHT; }
    if (insetBottom > best) { best = insetBottom; edge = ABE_BOTTOM; }

    // No reservation at all: the taskbar is already hidden, or on another
    // monitor. Caller keeps whatever it had.
    if (best <= 0) {
        return 0xFFFFFFFF;
    }

    return edge;
}

using TrayUI_GetStuckPlace_t = UINT(WINAPI*)(void* pThis);
TrayUI_GetStuckPlace_t TrayUI_GetStuckPlace_Original = nullptr;
UINT WINAPI TrayUI_GetStuckPlace_Hook(void* pThis) {
    if (g_pretendBottom) {
        return ABE_BOTTOM;
    }
    return TrayUI_GetStuckPlace_Original(pThis);
}

using TrayUI_SetStuckPlace_t = void(WINAPI*)(void* pThis, UINT edge);
TrayUI_SetStuckPlace_t TrayUI_SetStuckPlace_Original;
void WINAPI TrayUI_SetStuckPlace_Hook(void* pThis, UINT edge) {
    InterlockedExchange(&g_currentEdge, (LONG)edge);
    DiagLog(L"SetStuckPlace(%u %s)", edge, EdgeName(edge));
    TrayUI_SetStuckPlace_Original(pThis, edge);
}

// Resolved but never hooked, so calling it from inside another hook cannot
// re-enter our own instrumentation.
UINT CurrentStuckPlace(void* pThis) {
    if (!TrayUI_GetStuckPlace_Original || !pThis) {
        return 0xFFFFFFFF;
    }
    return TrayUI_GetStuckPlace_Original(pThis);
}

// --------------------------------------------------------- state/flags hooks

using TrayUI_GetAutoHideFlags_t = UINT(WINAPI*)(void* pThis);
TrayUI_GetAutoHideFlags_t TrayUI_GetAutoHideFlags_Original;
// TrayUI::GetAutoHideFlags is itself the gate. Disassembled:
//
//     lea  <feature flag>,%rcx
//     call <is feature enabled>
//     test %al,%al
//     je   .return_real
//     cmpl $0x3,0x118(%rbx)     ; edge == ABE_BOTTOM ?
//     je   .return_real
//     xor  %eax,%eax            ; otherwise report no flags
//     ret
//   .return_real:
//     mov  0x44(%rbx),%eax      ; the genuinely stored flags
//
// The flags are stored correctly at this+0x44 on every edge; the getter masks
// them to zero off-bottom, and everything downstream is gated on the result.
//
// Reporting the real stored value is therefore the whole fix. Fabricating a
// value instead leaves this+0x44 disagreeing with the getter, which is why an
// earlier attempt hid the taskbar but never released the work area or armed
// the unhide.

// Defined further down, alongside the unhide handling.
bool CursorAtDockedEdge(UINT edge);
LRESULT CALLBACK TrayWatchProc(HWND, UINT, WPARAM, LPARAM, DWORD_PTR);
void LogTrayWindowRect(PCWSTR when);

// Cursor transitions are detected in GetAutoHideFlags, which runs constantly,
// but acted on in GetDockedRect, which is the only place with the right `this`
// base and which runs only on layout passes. Detecting and acting in the same
// place failed: GetDockedRect was called 22 times in a whole session and missed
// almost every transition. The nudge posted on each transition is what brings
// the layout pass along shortly after.
enum PendingAction { kPendingNone = 0, kPendingReveal, kPendingHide };
volatile LONG g_pendingAction = kPendingNone;
extern bool g_lastCursorAtEdge;
void NudgeTaskbarRelayout(bool cursorAtEdge);

const size_t kTrayUiFlagsOffset = 0x44;
const size_t kTrayUiEdgeOffset = 0x118;

// The two offsets above are read out of GetAutoHideFlags itself, so verify the
// function still looks the way it was decoded before trusting them. The
// sequence encodes both: cmpl $0x3,0x118(%rbx) / xor %eax,%eax / mov 0x44(%rbx).
const size_t kFlagsGateSigOffset = 0x19;
const BYTE kFlagsGateSig[] = {
    0x83, 0xBB, 0x18, 0x01, 0x00, 0x00, 0x03,  // cmpl $0x3,0x118(%rbx)
    0x74, 0x04,                                // je   .return_real
    0x33, 0xC0,                                // xor  %eax,%eax
    0xEB, 0x03,                                // jmp
    0x8B, 0x43, 0x44,                          // mov  0x44(%rbx),%eax
};

bool g_flagsOffsetsVerified = false;

struct Settings {
    bool edgeLeft = true;
    bool edgeTop = true;
    bool edgeRight = true;
    int revealTolerance = 4;
} g_settings;

bool EdgeEnabled(UINT edge) {
    switch (edge) {
        case ABE_LEFT:  return g_settings.edgeLeft;
        case ABE_TOP:   return g_settings.edgeTop;
        case ABE_RIGHT: return g_settings.edgeRight;
        default:        return false;
    }
}

void LoadSettings() {
    g_settings.edgeLeft = Wh_GetIntSetting(L"edgeLeft") != 0;
    g_settings.edgeTop = Wh_GetIntSetting(L"edgeTop") != 0;
    g_settings.edgeRight = Wh_GetIntSetting(L"edgeRight") != 0;
    g_settings.revealTolerance = Wh_GetIntSetting(L"revealTolerance");
    if (g_settings.revealTolerance < 1 || g_settings.revealTolerance > 64) {
        g_settings.revealTolerance = 4;
    }
}

UINT WINAPI TrayUI_GetAutoHideFlags_Hook(void* pThis) {
    UINT ret = TrayUI_GetAutoHideFlags_Original(pThis);
    if (!pThis || !g_flagsOffsetsVerified) {
        return ret;
    }

    // Read the edge from the same member the function itself uses, so this
    // cannot disagree with the original.
    UINT edge = *(UINT*)((BYTE*)pThis + kTrayUiEdgeOffset);
    if (edge >= ABE_BOTTOM || !EdgeEnabled(edge)) {
        return ret;   // bottom works natively; anything higher is implausible
    }

    UINT stored = *(UINT*)((BYTE*)pThis + kTrayUiFlagsOffset);

    // Detect cursor transitions here because this runs constantly; the action
    // is performed on the next layout pass in GetDockedRect, which owns the
    // correct `this` base.
    bool atEdge = CursorAtDockedEdge(edge);

    if (atEdge != g_lastCursorAtEdge) {
        if (atEdge && (stored & 3) == 3) {
            InterlockedExchange(&g_pendingAction, kPendingReveal);
        } else if (!atEdge && (stored & 1) && !(stored & 2)) {
            InterlockedExchange(&g_pendingAction, kPendingHide);
        }
    }

    NudgeTaskbarRelayout(atEdge);

    if (stored != ret) {
        DiagLog(L"GetAutoHideFlags: stored 0x%X, masked to 0x%X [edge=%u %s]",
                stored, ret, edge, EdgeName(edge));
    }
    return stored;
}


// 0x3 is not a configuration value, it is the "currently hidden" state:
// GetDockedRect returns the hidden rect exactly when (flags & 3) == 3. Windows
// toggles 0x1 (shown) / 0x3 (hidden). Forcing 0x3 unconditionally pins the
// taskbar hidden, so Unhide and SlideWindow(show=1) run and are immediately
// overruled by the next rect query.
//
// While Windows is unhiding, its SetAutoHideFlags(0x1) means "show" and must
// be passed through untouched.
thread_local bool g_inUnhide = false;

const int kUnhideRequestStateChanged = 9;

// Request 9 is used both for Windows' "state changed, ensure shown" reflex and
// for a genuine hover reveal, so the code alone cannot tell them apart. The
// cursor can: a real reveal has it against the docked edge. Checked in-process
// with GetCursorPos -- no global input hooks.
RECT g_trayMonitorRect{};
ULONGLONG g_trayMonitorRectAt = 0;

// GetAutoHideFlags is extremely hot, so cache the monitor rect and refresh it
// only occasionally. FindWindow/GetMonitorInfo on every call would not do.
bool TrayMonitorRect(RECT* out) {
    ULONGLONG now = GetTickCount64();
    if (now - g_trayMonitorRectAt > 2000 || g_trayMonitorRect.right == 0) {
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
        g_trayMonitorRect = mi.rcMonitor;
        g_trayMonitorRectAt = now;
    }
    *out = g_trayMonitorRect;
    return true;
}

// Ask the shell to re-read its own layout. WM_SETTINGCHANGE/SPI_SETWORKAREA is
// a public, side-effect-light way to do that from any thread, and avoids
// calling any TrayUI member from outside its own call flow -- which is what
// took Explorer down when the V1 driver tried it.
bool g_lastCursorAtEdge = false;
ULONGLONG g_lastNudgeAt = 0;

// Everything so far has inferred the window's position from SlideWindow being
// called. Measure it instead.
void LogTrayWindowRect(PCWSTR when) {
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tray) {
        DiagLog(L"trayrect[%s]: no window", when);
        return;
    }
    RECT r{};
    GetWindowRect(tray, &r);
    DiagLog(L"trayrect[%s]: (%ld,%ld)-(%ld,%ld) visible=%d", when, r.left,
            r.top, r.right, r.bottom, (int)IsWindowVisible(tray));
}

void NudgeTaskbarRelayout(bool cursorAtEdge) {
    if (cursorAtEdge == g_lastCursorAtEdge) {
        return;   // only on transition
    }
    g_lastCursorAtEdge = cursorAtEdge;

    ULONGLONG now = GetTickCount64();
    if (now - g_lastNudgeAt < 150) {
        return;   // debounce
    }
    g_lastNudgeAt = now;

    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tray) {
        return;
    }

    DiagLog(L"nudge relayout: cursorAtEdge=%d", (int)cursorAtEdge);
    LogTrayWindowRect(cursorAtEdge ? L"cursor-arrived" : L"cursor-left");
    PostMessageW(tray, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
}

bool CursorAtDockedEdge(UINT edge) {
    POINT pt;
    if (!GetCursorPos(&pt)) {
        return false;
    }

    RECT m{};
    if (!TrayMonitorRect(&m)) {
        return false;
    }

    bool onMonitor = !(pt.x < m.left || pt.x > m.right || pt.y < m.top ||
                       pt.y > m.bottom);

    const LONG tol = g_settings.revealTolerance;
    bool result = false;
    if (onMonitor) {
        switch (edge) {
            case ABE_LEFT:  result = pt.x <= m.left + tol; break;
            case ABE_TOP:   result = pt.y <= m.top + tol; break;
            case ABE_RIGHT: result = pt.x >= m.right - tol; break;
            default:        result = false; break;
        }
    }

    // Throttled: this runs on every GetAutoHideFlags call. Every conclusion
    // about the cursor so far has been inferred rather than measured.
    static ULONGLONG lastLog = 0;
    ULONGLONG now = GetTickCount64();
    if (now - lastLog > 1000) {
        lastLog = now;
        DiagLog(L"cursor(%ld,%ld) monitor(%ld,%ld)-(%ld,%ld) edge=%u "
                L"onMon=%d result=%d",
                pt.x, pt.y, m.left, m.top, m.right, m.bottom, edge,
                (int)onMonitor, (int)result);
    }

    return result;
}

using TrayUI_SetAutoHideFlags_t = void(WINAPI*)(void* pThis, UINT flags);
TrayUI_SetAutoHideFlags_t TrayUI_SetAutoHideFlags_Original;
void WINAPI TrayUI_SetAutoHideFlags_Hook(void* pThis, UINT flags) {
    UINT edge = CurrentStuckPlace(pThis);
    if (edge <= 3) {
        InterlockedExchange(&g_currentEdge, (LONG)edge);
    }
    InterlockedExchange(&g_desiredFlags, (LONG)flags);

    // On the bottom edge Windows stores ABS_AUTOHIDE and then follows up with
    // ABS_AUTOHIDE|ABS_ALWAYSONTOP (0x3). Off-bottom the follow-up never
    // happens, so the stored value stays 0x1 -- and GetDockedRect computes the
    // hidden rect only when (flags & 3) == 3, so nothing ever hides.
    //
    // Store the same 0x3 the bottom edge stores. Doing it here rather than
    // faking it in the getter keeps this+0x44 and GetAutoHideFlags in
    // agreement; an earlier attempt faked it in the getter and the taskbar hid
    // without the work area being released or unhide being armed.
    // Use the edge from GetStuckPlace, not a raw member read. GetDockedRect
    // does `add $-8,%rcx` before calling the getter, so GetAutoHideFlags sees a
    // `this` 8 bytes below the base these other methods use -- which is why the
    // edge sits at 0x118 there but 0x110 in GetDockedRect. Reusing the getter's
    // offset here read the wrong field entirely.
    UINT effective = flags;
    if ((flags & 1) && edge < ABE_BOTTOM && EdgeEnabled(edge) && !g_inUnhide &&
        !CursorAtDockedEdge(edge)) {
        effective = flags | 2;
    }

    DiagLog(L"SetAutoHideFlags(0x%X)%s ENTER [edge=%u %s]", flags,
            effective != flags ? L" -> 0x3" : L"", edge, EdgeName(edge));
    TrayUI_SetAutoHideFlags_Original(pThis, effective);
    DiagLog(L"SetAutoHideFlags(0x%X)  LEAVE", flags);

}


using TrayUI_AutoHideChangedOnMonitor_t = void(WINAPI*)(void* pThis,
                                                        HMONITOR monitor);
TrayUI_AutoHideChangedOnMonitor_t TrayUI_AutoHideChangedOnMonitor_Original;
void WINAPI TrayUI_AutoHideChangedOnMonitor_Hook(void* pThis,
                                                 HMONITOR monitor) {
    UINT edge = CurrentStuckPlace(pThis);
    DiagLog(L"AutoHideChangedOnMonitor(%p) ENTER [edge=%u %s]", (void*)monitor,
            edge, EdgeName(edge));
    TrayUI_AutoHideChangedOnMonitor_Original(pThis, monitor);
    DiagLog(L"AutoHideChangedOnMonitor(%p) LEAVE", (void*)monitor);
}

// ----------------------------------------------------- geometry/motion hooks

using TrayUI_ComputeHiddenRect_t = void(WINAPI*)(void* pThis, RECT* rect,
                                                 UINT edge);

using TrayUI_GetDockedRect_t = UINT(WINAPI*)(void* pThis, RECT* rect,
                                             int param);
TrayUI_GetDockedRect_t TrayUI_GetDockedRect_Original;
// Windows detects "hidden and the cursor is at my edge" itself and issues
// Unhide(flags=2, request=1) -- that is the one request code observed to
// actually slide the taskbar back. Off-bottom that detection never runs, so
// hovering does nothing while a click still reveals it via activation.
//
// Issue that same call ourselves on cursor arrival. This is done from
// GetDockedRect rather than from a timer: it runs on the taskbar's own thread
// and shares Unhide's `this` base (both use 0x110/0x3c, versus the getter's
// 0x118/0x44 on a base 8 lower). The V1 driver crashed precisely because it
// called TrayUI members from the wrong thread with an unverified pointer.
using TrayUI_Unhide_t = void(WINAPI*)(void* pThis, int flags, int request);
extern TrayUI_Unhide_t TrayUI_Unhide_Original;
using TrayUI_SetAutoHideFlags_t2 = void(WINAPI*)(void* pThis, UINT flags);
extern TrayUI_SetAutoHideFlags_t TrayUI_SetAutoHideFlags_Original;

thread_local bool g_inSelfUnhide = false;

UINT WINAPI TrayUI_GetDockedRect_Hook(void* pThis, RECT* rect, int param) {
    UINT ret = TrayUI_GetDockedRect_Original(pThis, rect, param);

    // This function's `this` is the base Unhide and SetAutoHideFlags expect.
    extern void* g_trayUI;
    g_trayUI = pThis;

    LONG pending = InterlockedExchange(&g_pendingAction, kPendingNone);
    if (pending != kPendingNone && !g_inSelfUnhide && pThis &&
        TrayUI_Unhide_Original && TrayUI_SetAutoHideFlags_Original &&
        g_flagsOffsetsVerified) {
        UINT edge = *(UINT*)((BYTE*)pThis + 0x110);
        if (edge < ABE_BOTTOM) {
            UINT stored = *(UINT*)((BYTE*)pThis + 0x3c);

            if (pending == kPendingReveal && (stored & 3) == 3) {
                DiagLog(L"self-unhide: revealing, Unhide(2,1)");
                LogTrayWindowRect(L"before-unhide");
                g_inSelfUnhide = true;
                TrayUI_Unhide_Original(pThis, 2, 1);
                g_inSelfUnhide = false;
                LogTrayWindowRect(L"after-unhide");
            } else if (pending == kPendingHide && (stored & 1) &&
                       !(stored & 2)) {
                DiagLog(L"self-hide: storing 0x%X", stored | 2);
                LogTrayWindowRect(L"before-hide");
                g_inSelfUnhide = true;
                TrayUI_SetAutoHideFlags_Original(pThis, stored | 2);
                g_inSelfUnhide = false;
                LogTrayWindowRect(L"after-hide");
            }
        }
    }
    if (rect) {
        DiagLog(L"GetDockedRect(%d) -> 0x%X (%ld,%ld)-(%ld,%ld)", param, ret,
                rect->left, rect->top, rect->right, rect->bottom);
    } else {
        DiagLog(L"GetDockedRect(%d) -> 0x%X null rect", param, ret);
    }
    return ret;
}

using TrayUI_ShrinkRectToStuckEdge_t = void(WINAPI*)(void* pThis, RECT* rect,
                                                     int param);
TrayUI_ShrinkRectToStuckEdge_t TrayUI_ShrinkRectToStuckEdge_Original;
void WINAPI TrayUI_ShrinkRectToStuckEdge_Hook(void* pThis, RECT* rect,
                                              int param) {
    TrayUI_ShrinkRectToStuckEdge_Original(pThis, rect, param);
    if (rect) {
        DiagLog(L"ShrinkRectToStuckEdge(%d) -> (%ld,%ld)-(%ld,%ld)", param,
                rect->left, rect->top, rect->right, rect->bottom);
    }
}

using TrayUI_GetStuckInfo_t = void(WINAPI*)(void* pThis, RECT* rect,
                                            UINT* edge);

using TrayUI_SlideWindow_t = void(WINAPI*)(void* pThis, HWND hWnd,
                                           const RECT* rect, HMONITOR monitor,
                                           bool show, bool animate);
TrayUI_SlideWindow_t TrayUI_SlideWindow_Original;
void WINAPI TrayUI_SlideWindow_Hook(void* pThis, HWND hWnd, const RECT* rect,
                                    HMONITOR monitor, bool show, bool animate) {
    UINT edge = CurrentStuckPlace(pThis);
    DiagLog(L"*** SlideWindow show=%d animate=%d [edge=%u %s] rect=(%ld,%ld)-(%ld,%ld)",
            (int)show, (int)animate, edge, EdgeName(edge),
            rect ? rect->left : 0, rect ? rect->top : 0,
            rect ? rect->right : 0, rect ? rect->bottom : 0);
    TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, show, animate);
    LogTrayWindowRect(show ? L"after-slide-show" : L"after-slide-hide");
    DiagLog(L"*** SlideWindow LEAVE show=%d", (int)show);
}

using TrayUI_Unhide_t = void(WINAPI*)(void* pThis, int flags, int request);
TrayUI_Unhide_t TrayUI_Unhide_Original;
void WINAPI TrayUI_Unhide_Hook(void* pThis, int flags, int request) {
    UINT edge = CurrentStuckPlace(pThis);
    DiagLog(L"Unhide(flags=%d request=%d) ENTER [edge=%u %s]", flags, request,
            edge, EdgeName(edge));

    // request 9 is Windows' "auto-hide state changed, ensure shown" reflex. It
    // fires in the same millisecond as our forced 0x3 because we inject the
    // hidden state at a moment Windows has not itself decided to hide, and it
    // immediately undoes the hide. On the bottom edge the 0x1->0x3 transition
    // is Windows' own, so no such Unhide follows.
    //
    // Genuine reveals use other request codes -- hovering the edge produced
    // request 1 in the pre-fix traces, and disabling produced request 5 -- so
    // suppressing 9 off-bottom leaves real unhides untouched.
    if (request == kUnhideRequestStateChanged && edge < ABE_BOTTOM &&
        ((UINT)g_desiredFlags & 1)) {
        if (CursorAtDockedEdge(edge)) {
            DiagLog(L"Unhide: request=9 with cursor at edge, allowing reveal");
        } else {
            DiagLog(L"Unhide: suppressing request=9 reflex, cursor away "
                    L"[edge=%u %s]",
                    edge, EdgeName(edge));
            return;
        }
    }

    bool prev = g_inUnhide;
    g_inUnhide = true;
    TrayUI_Unhide_Original(pThis, flags, request);
    g_inUnhide = prev;

    DiagLog(L"Unhide LEAVE");
}



// -------------------------------------------------------------- edge watcher

// GetAutoHideFlags is not a hot path: it fires in bursts around state changes
// and not at all while the taskbar sits hidden. A run with the cursor moved
// repeatedly along the left edge produced two cursor samples, 28 seconds apart.
// So detection needs a timer; there is no polling site among the hooks.
//
// V1 also used a timer and crashed Explorer. The cause was not the timer or the
// thread: it called _ComputeHiddenRect, which GetDockedRect invokes as
// `lea -0x30(%rdi),%rcx` -- a base 0x30 lower than the caller's. Passing the
// unadjusted pointer meant a wrong `this`. Unhide and SetAutoHideFlags share
// GetDockedRect's base, and Unhide has already been called that way without
// incident, so this watcher touches only those two and never computes rects.

#define WATCH_TIMER_ID 0x57484132   // 'WHA2'
#define WATCH_TICK_MS  120

void* g_trayUI = nullptr;   // captured in GetDockedRect: correct base
HWND g_trayWnd = nullptr;
bool g_subclassed = false;
bool g_timerSet = false;
thread_local bool g_inWatcherCall = false;
int g_leaveTicks = 0;

void WatcherTick() {
    if (!g_trayUI || !g_flagsOffsetsVerified || g_inWatcherCall ||
        !TrayUI_Unhide_Original || !TrayUI_SetAutoHideFlags_Original ||
        !TrayUI_GetDockedRect_Original) {
        return;
    }

    UINT edge = *(UINT*)((BYTE*)g_trayUI + 0x110);
    if (edge >= ABE_BOTTOM || !EdgeEnabled(edge)) {
        return;
    }

    UINT stored = *(UINT*)((BYTE*)g_trayUI + 0x3c);
    if (!(stored & 1)) {
        return;   // auto-hide not enabled
    }

    // Two different regions, which is what auto-hide actually needs:
    //   hidden -> reveal when the cursor reaches the thin trigger strip
    //   shown  -> stay up while the cursor is anywhere over the taskbar
    // Using the 4px trigger for both meant that at x=20, over a revealed 48px
    // taskbar, the cursor counted as "away" and it hid immediately.
    if (stored & 2) {
        if (!CursorAtDockedEdge(edge)) {
            g_leaveTicks = 0;
            return;
        }

        DiagLog(L"watcher: cursor at edge, Unhide(2,1)");
        LogTrayWindowRect(L"before-unhide");
        g_inWatcherCall = true;
        TrayUI_Unhide_Original(g_trayUI, 2, 1);
        g_inWatcherCall = false;
        LogTrayWindowRect(L"after-unhide");
        g_leaveTicks = 0;
        return;
    }

    RECT docked{};
    TrayUI_GetDockedRect_Original(g_trayUI, &docked, 0);

    POINT pt{};
    if (!GetCursorPos(&pt)) {
        return;
    }

    // Stay up while the cursor is over the taskbar, or while the taskbar is the
    // active window, which covers Start being open.
    if (PtInRect(&docked, pt) || GetForegroundWindow() == g_trayWnd) {
        g_leaveTicks = 0;
        return;
    }

    // A couple of ticks outside, so a fast pass along the edge cannot flap it.
    if (++g_leaveTicks < 2) {
        return;
    }
    g_leaveTicks = 0;

    DiagLog(L"watcher: cursor left the taskbar, storing 0x%X", stored | 2);
    LogTrayWindowRect(L"before-hide");

    g_inWatcherCall = true;
    TrayUI_SetAutoHideFlags_Original(g_trayUI, stored | 2);

    if (TrayUI_SlideWindow_Original && g_trayWnd) {
        RECT hiddenRect{};
        TrayUI_GetDockedRect_Original(g_trayUI, &hiddenRect, 0);
        DiagLog(L"watcher: sliding to (%ld,%ld)-(%ld,%ld)", hiddenRect.left,
                hiddenRect.top, hiddenRect.right, hiddenRect.bottom);
        TrayUI_SlideWindow_Original(
            g_trayUI, g_trayWnd, &hiddenRect,
            MonitorFromWindow(g_trayWnd, MONITOR_DEFAULTTOPRIMARY), false,
            true);
    }

    g_inWatcherCall = false;
    LogTrayWindowRect(L"after-hide");
}

LRESULT CALLBACK TrayWatchProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                               LPARAM lParam, DWORD_PTR dwRefData) {
    if (!g_timerSet) {
        // Set from the window's own thread, which is where messages arrive.
        SetTimer(hWnd, WATCH_TIMER_ID, WATCH_TICK_MS, nullptr);
        g_timerSet = true;
        DiagLog(L"watcher: timer armed on %p", (void*)hWnd);
    }

    if (uMsg == WM_TIMER && wParam == WATCH_TIMER_ID) {
        WatcherTick();
        return 0;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// ------------------------------------------------------------------ lifecycle

HMODULE g_taskbarModule = nullptr;

void Wh_ModSettingsChanged() {
    LoadSettings();
    DiagLog(L"settings: left=%d top=%d right=%d tol=%d",
            (int)g_settings.edgeLeft, (int)g_settings.edgeTop,
            (int)g_settings.edgeRight, g_settings.revealTolerance);
}

BOOL Wh_ModInit() {


    if (!IsSupportedBuild()) {
        DiagLog(L"=== UNSUPPORTED BUILD %lu, mod inactive ===", g_buildNumber);
                return FALSE;
    }

    DiagLog(L"=== diag mod init, build %lu, pid %lu ===", g_buildNumber,
            GetCurrentProcessId());

    g_taskbarModule =
        LoadLibraryExW(L"taskbar.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!g_taskbarModule) {
        DiagLog(L"FATAL: could not load taskbar.dll");
        return FALSE;
    }

    DiagLog(L"taskbar.dll loaded at %p", (void*)g_taskbarModule);

    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        // Resolved only; deliberately not hooked.
        {{LR"(public: virtual unsigned int __cdecl TrayUI::GetStuckPlace(void))", LR"(public: virtual unsigned int __cdecl TrayUI::GetStuckPlace(void) )"},
         &TrayUI_GetStuckPlace_Original, TrayUI_GetStuckPlace_Hook, true},

        {{LR"(public: virtual unsigned int __cdecl TrayUI::GetAutoHideFlags(void))", LR"(public: virtual unsigned int __cdecl TrayUI::GetAutoHideFlags(void) )"},
         &TrayUI_GetAutoHideFlags_Original, TrayUI_GetAutoHideFlags_Hook, true},


        {{LR"(public: virtual void __cdecl TrayUI::SetStuckPlace(unsigned int))", LR"(public: virtual void __cdecl TrayUI::SetStuckPlace(unsigned int) )"},
         &TrayUI_SetStuckPlace_Original, TrayUI_SetStuckPlace_Hook, true},

        {{LR"(public: virtual void __cdecl TrayUI::SetAutoHideFlags(unsigned int))", LR"(public: virtual void __cdecl TrayUI::SetAutoHideFlags(unsigned int) )"},
         &TrayUI_SetAutoHideFlags_Original, TrayUI_SetAutoHideFlags_Hook, true},



        {{LR"(public: virtual unsigned int __cdecl TrayUI::GetDockedRect(struct tagRECT *,int))", LR"(public: virtual unsigned int __cdecl TrayUI::GetDockedRect(struct tagRECT *,int) )"},
         &TrayUI_GetDockedRect_Original, TrayUI_GetDockedRect_Hook, true},



        {{LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool))", LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool) )"},
         &TrayUI_SlideWindow_Original, TrayUI_SlideWindow_Hook, true},

        {{LR"(public: virtual void __cdecl TrayUI::Unhide(enum TrayCommon::TrayUnhideFlags,enum TrayCommon::UnhideRequest))", LR"(public: virtual void __cdecl TrayUI::Unhide(enum TrayCommon::TrayUnhideFlags,enum TrayCommon::UnhideRequest) )"},
         &TrayUI_Unhide_Original, TrayUI_Unhide_Hook, true},


    };

    if (!WindhawkUtils::HookSymbols(g_taskbarModule, hooks, ARRAYSIZE(hooks))) {
        DiagLog(L"FATAL: HookSymbols failed");
        return FALSE;
    }

    // Every entry is optional so a single rename degrades instead of killing
    // the mod, but a diagnosis without these two is meaningless.
    // Report every symbol individually. A silently-empty resolve previously
    // looked like a successful run, which cost a full diagnostic round-trip.
    struct {
        PCWSTR name;
        void* resolved;
    } checks[] = {
        {L"GetStuckPlace", (void*)TrayUI_GetStuckPlace_Original},
        {L"GetAutoHideFlags", (void*)TrayUI_GetAutoHideFlags_Original},
        {L"SetAutoHideFlags", (void*)TrayUI_SetAutoHideFlags_Original},
        {L"GetDockedRect", (void*)TrayUI_GetDockedRect_Original},
        {L"SlideWindow", (void*)TrayUI_SlideWindow_Original},
        {L"Unhide", (void*)TrayUI_Unhide_Original},
    };

    int resolvedCount = 0;
    for (const auto& c : checks) {
        DiagLog(L"  symbol %-28s %s", c.name,
                c.resolved ? L"OK" : L"UNRESOLVED");
        if (c.resolved) {
            resolvedCount++;
        }
    }

    UINT seeded = DetectEdgeFromWorkArea();
    if (seeded != 0xFFFFFFFF) {
        InterlockedExchange(&g_currentEdge, (LONG)seeded);
    }
    if (TrayUI_GetAutoHideFlags_Original) {
        const BYTE* p = (const BYTE*)TrayUI_GetAutoHideFlags_Original +
                        kFlagsGateSigOffset;
        g_flagsOffsetsVerified =
            memcmp(p, kFlagsGateSig, sizeof(kFlagsGateSig)) == 0;
        DiagLog(L"flags gate signature verified: %d",
                (int)g_flagsOffsetsVerified);
        if (!g_flagsOffsetsVerified) {
            DiagLog(L"WARNING: GetAutoHideFlags does not match the decoded "
                    L"form; member offsets are not trustworthy, mod inert");
        }
    }

    DiagLog(L"seeded edge from work area: %u %s", (UINT)g_currentEdge,
            EdgeName((UINT)g_currentEdge));

    g_trayWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (g_trayWnd) {
        g_subclassed = WindhawkUtils::SetWindowSubclassFromAnyThread(
                           g_trayWnd, TrayWatchProc, 0) != FALSE;
        DiagLog(L"watcher: subclassed %p ok=%d", (void*)g_trayWnd,
                (int)g_subclassed);
    } else {
        DiagLog(L"watcher: Shell_TrayWnd not found");
    }

    DiagLog(L"hooks installed: %d/%d resolved", resolvedCount,
            (int)ARRAYSIZE(checks));

    // All-or-nothing: every one of these is load-bearing, and running with any
    // of them missing would change nothing while appearing to work.
    struct {
        PCWSTR name;
        void* ptr;
    } required[] = {
        {L"GetAutoHideFlags", (void*)TrayUI_GetAutoHideFlags_Original},
        {L"SetAutoHideFlags", (void*)TrayUI_SetAutoHideFlags_Original},
        {L"GetDockedRect", (void*)TrayUI_GetDockedRect_Original},
        {L"GetStuckPlace", (void*)TrayUI_GetStuckPlace_Original},
        {L"SlideWindow", (void*)TrayUI_SlideWindow_Original},
        {L"Unhide", (void*)TrayUI_Unhide_Original},
    };

    for (const auto& r : required) {
        if (!r.ptr) {
            DiagLog(L"required symbol %s unresolved, mod inactive", r.name);
            return FALSE;
        }
    }

    if (!g_flagsOffsetsVerified) {
        DiagLog(L"GetAutoHideFlags does not match the decoded form, "
                L"mod inactive");
        return FALSE;
    }

    return TRUE;
}

void Wh_ModUninit() {
    if (g_trayWnd && g_timerSet) {
        KillTimer(g_trayWnd, WATCH_TIMER_ID);
    }
    if (g_subclassed && g_trayWnd) {
        WindhawkUtils::RemoveWindowSubclassFromAnyThread(g_trayWnd,
                                                         TrayWatchProc);
    }

    DiagLog(L"=== diag mod uninit ===");

    // Clear the guard before closing so any hook still in flight on another
    // thread bails out at the top of DiagLog instead of writing to a closed
    // handle.

    // Deliberately NOT DeleteCriticalSection: a hook thread can already be
    // past the guard check and blocked on EnterCriticalSection, and deleting
    // it underneath that thread is undefined behaviour. The section lives for
    // the lifetime of the process; leaking it costs nothing and removes the
    // race entirely.
}
