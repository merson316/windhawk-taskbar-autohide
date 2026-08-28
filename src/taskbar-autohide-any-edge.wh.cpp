// ==WindhawkMod==
// @id              taskbar-autohide-any-edge
// @name            Taskbar auto-hide on any edge
// @description     Read-only diagnostic. Logs the taskbar auto-hide call chain to a file. Changes no behaviour.
// @version         0.1.0
// @author          merson316
// @include         explorer.exe
// @architecture    x86-64
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar auto-hide any edge (diagnostic)

Read-only instrumentation for Windows 11 26H2. Logs which functions in
`TrayUI`'s auto-hide chain are called when the auto-hide setting is toggled
while the taskbar is docked to a non-bottom edge.

Writes to `%LOCALAPPDATA%\windhawk-autohide-fix.log`. Changes no behaviour:
every hook calls the original and returns its result unmodified.

This mod is a diagnostic step, not a fix. Disable it once the log is captured.
*/
// ==/WindhawkModReadme==

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

HANDLE g_logFile = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_logLock;
bool g_logLockReady = false;

void DiagLogOpen() {
    WCHAR base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return;
    }

    std::wstring path(base);
    path += L"\\windhawk-autohide-fix.log";

    // FILE_APPEND_DATA plus permissive sharing so the log stays readable from
    // another process while Explorer holds it open.
    g_logFile = CreateFileW(
        path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void DiagLogClose() {
    if (g_logFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_logFile);
        g_logFile = INVALID_HANDLE_VALUE;
    }
}

void DiagLog(PCWSTR fmt, ...) {
    if (g_logFile == INVALID_HANDLE_VALUE || !g_logLockReady) {
        return;
    }

    WCHAR line[1024];

    SYSTEMTIME st;
    GetLocalTime(&st);

    int off = swprintf(line, ARRAYSIZE(line), L"%02u:%02u:%02u.%03u [t%5lu] ",
                       st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                       GetCurrentThreadId());
    if (off < 0) {
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    int n = vswprintf(line + off, ARRAYSIZE(line) - off - 3, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }

    size_t len = wcslen(line);
    line[len] = L'\r';
    line[len + 1] = L'\n';
    line[len + 2] = L'\0';

    char utf8[2048];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8),
                                    nullptr, nullptr);
    if (bytes <= 1) {
        return;
    }

    EnterCriticalSection(&g_logLock);
    DWORD written = 0;
    // bytes includes the NUL terminator; do not write it.
    WriteFile(g_logFile, utf8, (DWORD)(bytes - 1), &written, nullptr);
    LeaveCriticalSection(&g_logLock);
}

// ------------------------------------------------------------- edge accessor

// ABE_LEFT=0, ABE_TOP=1, ABE_RIGHT=2, ABE_BOTTOM=3.
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

UINT WINAPI TrayUI_GetAutoHideFlags_Hook(void* pThis) {
    UINT ret = TrayUI_GetAutoHideFlags_Original(pThis);
    if (!pThis || !g_flagsOffsetsVerified) {
        return ret;
    }

    // Read the edge from the same member the function itself uses, so this
    // cannot disagree with the original.
    UINT edge = *(UINT*)((BYTE*)pThis + kTrayUiEdgeOffset);
    if (edge >= ABE_BOTTOM) {
        return ret;   // bottom works natively; anything higher is implausible
    }

    UINT stored = *(UINT*)((BYTE*)pThis + kTrayUiFlagsOffset);

    // Windows never issues an unhide request off-bottom -- the cursor-at-edge
    // detection that normally does so does not run. But GetDockedRect chooses
    // the hidden rect purely on (flags & 3) == 3, so clearing the hidden bit
    // while the cursor is at the edge is itself the reveal.
    //
    // Answering correctly is not enough on its own: nothing re-reads the rect
    // unless a layout pass happens, which is why the taskbar stayed parked at
    // the hidden rect while remaining clickable through its 2px sliver. Nudge
    // one on each transition.
    // Do NOT clear the hidden bit on hover. Unhide inspects this value to
    // decide whether there is anything to reveal: reporting "already shown"
    // made it a no-op, which is why hovering produced Unhide(request=8) and
    // (request=18) that slid nothing, while the one successful reveal happened
    // when the cursor had left and the true hidden state was reported.
    //
    // Report the truth and let Unhide clear the bit itself via
    // SetAutoHideFlags(0x1), which the in-unhide passthrough leaves untouched.
    bool atEdge = CursorAtDockedEdge(edge);
    NudgeTaskbarRelayout(atEdge);

    // The getter reports the stored value and nothing else. Synthesising a
    // different hidden state here created two competing sources of truth: the
    // stored flags said "shown" while this said "hidden", so the layout undid
    // every reveal and the self-unhide never re-armed. Transitions are driven
    // in GetDockedRect instead, against the stored state.
    (void)atEdge;

    if (stored != ret) {
        DiagLog(L"GetAutoHideFlags: stored 0x%X, masked to 0x%X [edge=%u %s]",
                stored, ret, edge, EdgeName(edge));
    }
    return stored;
}

using TrayUI_GetDesktopModeAutoHide_t = UINT(WINAPI*)(void* pThis);
TrayUI_GetDesktopModeAutoHide_t TrayUI_GetDesktopModeAutoHide_Original;
UINT WINAPI TrayUI_GetDesktopModeAutoHide_Hook(void* pThis) {
    UINT ret = TrayUI_GetDesktopModeAutoHide_Original(pThis);
    UINT edge = CurrentStuckPlace(pThis);
    DiagLog(L"GetDesktopModeAutoHide -> 0x%X   [edge=%u %s]", ret, edge,
            EdgeName(edge));
    return ret;
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

    if (pt.x < m.left || pt.x > m.right || pt.y < m.top || pt.y > m.bottom) {
        return false;   // not on the taskbar's monitor
    }

    if (pt.x < m.left || pt.x > m.right || pt.y < m.top || pt.y > m.bottom) {
        return false;   // not on the taskbar's monitor
    }

    const LONG tol = 4;
    switch (edge) {
        case ABE_LEFT:  return pt.x <= m.left + tol;
        case ABE_TOP:   return pt.y <= m.top + tol;
        case ABE_RIGHT: return pt.x >= m.right - tol;
        default:        return false;
    }
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
    if ((flags & 1) && edge < ABE_BOTTOM && !g_inUnhide &&
        !CursorAtDockedEdge(edge)) {
        effective = flags | 2;
    }

    DiagLog(L"SetAutoHideFlags(0x%X)%s ENTER [edge=%u %s]", flags,
            effective != flags ? L" -> 0x3" : L"", edge, EdgeName(edge));
    TrayUI_SetAutoHideFlags_Original(pThis, effective);
    DiagLog(L"SetAutoHideFlags(0x%X)  LEAVE", flags);

}

using TrayUI_SetAutoHideState_t = LONG(WINAPI*)(void* pThis, int state);
TrayUI_SetAutoHideState_t TrayUI_SetAutoHideState_Original;
LONG WINAPI TrayUI_SetAutoHideState_Hook(void* pThis, int state) {
    // `this` is adjusted here, so use the tracked edge rather than pThis.
    if (!g_pretendBottom) {
        UINT detected = DetectEdgeFromWorkArea();
        if (detected != 0xFFFFFFFF) {
            InterlockedExchange(&g_currentEdge, (LONG)detected);
        }
    }

    UINT realEdge = (UINT)g_currentEdge;

    // Spoofing GetStuckPlace here was tried and disproved: the gate inside
    // SetAutoHideState does not read the edge that way. It calls a predicate
    // which is hooked directly instead -- see AutoHideSuppressed_Hook.
    DiagLog(L"SetAutoHideState(%d) ENTER [realEdge=%u %s]", state, realEdge,
            EdgeName(realEdge));

    LONG ret = TrayUI_SetAutoHideState_Original(pThis, state);

    DiagLog(L"SetAutoHideState(%d) LEAVE -> 0x%08X", state, ret);
    return ret;
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
TrayUI_ComputeHiddenRect_t TrayUI_ComputeHiddenRect_Original;
void WINAPI TrayUI_ComputeHiddenRect_Hook(void* pThis, RECT* rect, UINT edge) {
    // While spoofing, the caller passes BOTTOM. Restore the true edge so the
    // taskbar hides towards the edge it is actually docked to.
    UINT realEdge = (UINT)g_currentEdge;
    if (g_pretendBottom && edge == ABE_BOTTOM && realEdge != ABE_BOTTOM) {
        DiagLog(L"_ComputeHiddenRect edge override %u -> %u %s", edge, realEdge,
                EdgeName(realEdge));
        edge = realEdge;
    }

    DiagLog(L"_ComputeHiddenRect(edge=%u %s) ENTER this=%p", edge,
            EdgeName(edge), pThis);
    TrayUI_ComputeHiddenRect_Original(pThis, rect, edge);
    if (rect) {
        DiagLog(L"_ComputeHiddenRect LEAVE -> (%ld,%ld)-(%ld,%ld)", rect->left,
                rect->top, rect->right, rect->bottom);
    } else {
        DiagLog(L"_ComputeHiddenRect LEAVE -> null rect");
    }
}

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
bool g_cursorWasAtEdge = false;

UINT WINAPI TrayUI_GetDockedRect_Hook(void* pThis, RECT* rect, int param) {
    UINT ret = TrayUI_GetDockedRect_Original(pThis, rect, param);

    if (!g_inSelfUnhide && pThis && TrayUI_Unhide_Original &&
        TrayUI_SetAutoHideFlags_Original && g_flagsOffsetsVerified) {
        UINT edge = *(UINT*)((BYTE*)pThis + 0x110);
        if (edge < ABE_BOTTOM) {
            UINT stored = *(UINT*)((BYTE*)pThis + 0x3c);
            bool atEdge = CursorAtDockedEdge(edge);

            if (atEdge && !g_cursorWasAtEdge && (stored & 3) == 3) {
                // Arrived: reveal. Unhide clears the hidden bit itself.
                DiagLog(L"self-unhide: cursor arrived, Unhide(2,1)");
                g_inSelfUnhide = true;
                TrayUI_Unhide_Original(pThis, 2, 1);
                g_inSelfUnhide = false;
            } else if (!atEdge && g_cursorWasAtEdge && (stored & 1) &&
                       !(stored & 2)) {
                // Left: re-hide by storing the hidden bit, as the bottom edge
                // does. The layout pass then moves it to the hidden rect.
                DiagLog(L"self-hide: cursor left, storing 0x%X", stored | 2);
                g_inSelfUnhide = true;
                TrayUI_SetAutoHideFlags_Original(pThis, stored | 2);
                g_inSelfUnhide = false;
            }

            g_cursorWasAtEdge = atEdge;
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
TrayUI_GetStuckInfo_t TrayUI_GetStuckInfo_Original;
void WINAPI TrayUI_GetStuckInfo_Hook(void* pThis, RECT* rect, UINT* edge) {
    TrayUI_GetStuckInfo_Original(pThis, rect, edge);
    DiagLog(L"GetStuckInfo -> edge=%u %s rect=(%ld,%ld)-(%ld,%ld)",
            edge ? *edge : 0xFFFFFFFF, EdgeName(edge ? *edge : 0xFFFFFFFF),
            rect ? rect->left : 0, rect ? rect->top : 0,
            rect ? rect->right : 0, rect ? rect->bottom : 0);
}

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

using TrayUI_SetUnhideTimer_t = void(WINAPI*)(void* pThis, LONG a, LONG b);
TrayUI_SetUnhideTimer_t TrayUI_SetUnhideTimer_Original;
void WINAPI TrayUI_SetUnhideTimer_Hook(void* pThis, LONG a, LONG b) {
    DiagLog(L"SetUnhideTimer(%ld, %ld)", a, b);
    TrayUI_SetUnhideTimer_Original(pThis, a, b);
}

using TrayUI_AutoHideCollision_t = void(WINAPI*)(void* pThis);
TrayUI_AutoHideCollision_t TrayUI_AutoHideCollision_Original;
void WINAPI TrayUI_AutoHideCollision_Hook(void* pThis) {
    UINT edge = CurrentStuckPlace(pThis);
    DiagLog(L"_AutoHideCollision ENTER [edge=%u %s]", edge, EdgeName(edge));
    TrayUI_AutoHideCollision_Original(pThis);
    DiagLog(L"_AutoHideCollision LEAVE");
}

// ------------------------------------------------------------------ lifecycle

HMODULE g_taskbarModule = nullptr;

BOOL Wh_ModInit() {
    InitializeCriticalSection(&g_logLock);
    g_logLockReady = true;

    DiagLogOpen();

    if (!IsSupportedBuild()) {
        DiagLog(L"=== UNSUPPORTED BUILD %lu, mod inactive ===", g_buildNumber);
        g_logLockReady = false;
        DiagLogClose();
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

        {{LR"(public: virtual unsigned int __cdecl TrayUI::GetDesktopModeAutoHide(void)const)", LR"(public: virtual unsigned int __cdecl TrayUI::GetDesktopModeAutoHide(void)const )"},
         &TrayUI_GetDesktopModeAutoHide_Original,
         TrayUI_GetDesktopModeAutoHide_Hook, true},

        {{LR"(public: virtual void __cdecl TrayUI::SetStuckPlace(unsigned int))", LR"(public: virtual void __cdecl TrayUI::SetStuckPlace(unsigned int) )"},
         &TrayUI_SetStuckPlace_Original, TrayUI_SetStuckPlace_Hook, true},

        {{LR"(public: virtual void __cdecl TrayUI::SetAutoHideFlags(unsigned int))", LR"(public: virtual void __cdecl TrayUI::SetAutoHideFlags(unsigned int) )"},
         &TrayUI_SetAutoHideFlags_Original, TrayUI_SetAutoHideFlags_Hook, true},

        {{LR"(public: virtual long __cdecl TrayUI::SetAutoHideState(int))", LR"(public: virtual long __cdecl TrayUI::SetAutoHideState(int) )"},
         &TrayUI_SetAutoHideState_Original, TrayUI_SetAutoHideState_Hook, true},

        {{LR"(public: void __cdecl TrayUI::AutoHideChangedOnMonitor(struct HMONITOR__ *))", LR"(public: void __cdecl TrayUI::AutoHideChangedOnMonitor(struct HMONITOR__ *) )"},
         &TrayUI_AutoHideChangedOnMonitor_Original,
         TrayUI_AutoHideChangedOnMonitor_Hook, true},
        {{LR"(public: void __cdecl TrayUI::_ComputeHiddenRect(struct tagRECT *,unsigned int))", LR"(public: void __cdecl TrayUI::_ComputeHiddenRect(struct tagRECT *,unsigned int) )"},
         &TrayUI_ComputeHiddenRect_Original, TrayUI_ComputeHiddenRect_Hook, true},

        {{LR"(public: virtual unsigned int __cdecl TrayUI::GetDockedRect(struct tagRECT *,int))", LR"(public: virtual unsigned int __cdecl TrayUI::GetDockedRect(struct tagRECT *,int) )"},
         &TrayUI_GetDockedRect_Original, TrayUI_GetDockedRect_Hook, true},

        {{LR"(private: void __cdecl TrayUI::ShrinkRectToStuckEdge(struct tagRECT &,int)const)", LR"(private: void __cdecl TrayUI::ShrinkRectToStuckEdge(struct tagRECT &,int)const )"},
         &TrayUI_ShrinkRectToStuckEdge_Original, TrayUI_ShrinkRectToStuckEdge_Hook, true},

        {{LR"(public: virtual void __cdecl TrayUI::GetStuckInfo(struct tagRECT *,unsigned int *))", LR"(public: virtual void __cdecl TrayUI::GetStuckInfo(struct tagRECT *,unsigned int *) )"},
         &TrayUI_GetStuckInfo_Original, TrayUI_GetStuckInfo_Hook, true},

        {{LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool))", LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool) )"},
         &TrayUI_SlideWindow_Original, TrayUI_SlideWindow_Hook, true},

        {{LR"(public: virtual void __cdecl TrayUI::Unhide(enum TrayCommon::TrayUnhideFlags,enum TrayCommon::UnhideRequest))", LR"(public: virtual void __cdecl TrayUI::Unhide(enum TrayCommon::TrayUnhideFlags,enum TrayCommon::UnhideRequest) )"},
         &TrayUI_Unhide_Original, TrayUI_Unhide_Hook, true},

        {{LR"(public: virtual void __cdecl TrayUI::SetUnhideTimer(long,long))", LR"(public: virtual void __cdecl TrayUI::SetUnhideTimer(long,long) )"},
         &TrayUI_SetUnhideTimer_Original, TrayUI_SetUnhideTimer_Hook, true},

        {{LR"(public: void __cdecl TrayUI::_AutoHideCollision(void))", LR"(public: void __cdecl TrayUI::_AutoHideCollision(void) )"},
         &TrayUI_AutoHideCollision_Original, TrayUI_AutoHideCollision_Hook, true},
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
        {L"GetDesktopModeAutoHide", (void*)TrayUI_GetDesktopModeAutoHide_Original},
        {L"SetAutoHideFlags", (void*)TrayUI_SetAutoHideFlags_Original},
        {L"SetAutoHideState", (void*)TrayUI_SetAutoHideState_Original},
        {L"AutoHideChangedOnMonitor", (void*)TrayUI_AutoHideChangedOnMonitor_Original},
        {L"_ComputeHiddenRect", (void*)TrayUI_ComputeHiddenRect_Original},
        {L"GetDockedRect", (void*)TrayUI_GetDockedRect_Original},
        {L"ShrinkRectToStuckEdge", (void*)TrayUI_ShrinkRectToStuckEdge_Original},
        {L"GetStuckInfo", (void*)TrayUI_GetStuckInfo_Original},
        {L"SlideWindow", (void*)TrayUI_SlideWindow_Original},
        {L"Unhide", (void*)TrayUI_Unhide_Original},
        {L"SetUnhideTimer", (void*)TrayUI_SetUnhideTimer_Original},
        {L"_AutoHideCollision", (void*)TrayUI_AutoHideCollision_Original},
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

    DiagLog(L"hooks installed: %d/%d resolved", resolvedCount,
            (int)ARRAYSIZE(checks));

    if (!TrayUI_SetAutoHideState_Original) {
        DiagLog(L"FATAL: SetAutoHideState unresolved, cannot diagnose");
        return FALSE;
    }
    return TRUE;
}

void Wh_ModUninit() {
    DiagLog(L"=== diag mod uninit ===");

    // Clear the guard before closing so any hook still in flight on another
    // thread bails out at the top of DiagLog instead of writing to a closed
    // handle.
    g_logLockReady = false;

    // Deliberately NOT DeleteCriticalSection: a hook thread can already be
    // past the guard check and blocked on EnterCriticalSection, and deleting
    // it underneath that thread is undefined behaviour. The section lives for
    // the lifetime of the process; leaking it costs nothing and removes the
    // race entirely.
    DiagLogClose();
}
