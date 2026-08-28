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
setting is accepted, reports success, and does nothing.

`TrayUI::GetAutoHideFlags` masks the auto-hide flags to zero unless the taskbar
is docked to the bottom. The flags are stored correctly on every edge; only the
read is masked, and everything downstream is gated on it, so no hide is ever
attempted. This mod unmasks the read, stores the same flag combination the
bottom edge uses, and supplies the pointer tracking that Windows only performs
on the bottom edge.

The bottom edge is left untouched.

## Known limitations

- No smooth show/hide animation: the Windows 11 easing is a XAML compositor
  animation that is never triggered off-bottom.
- Primary taskbar only. Secondary taskbars use a separate `CSecondaryTray`
  class.
- Requires Windows 11 build 26200 or newer.
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
- hideDurationMs: 0
  $name: Hide animation duration (ms)
  $description: >-
    0 keeps the Windows default (400 ms). A vertical taskbar covers far less
    screen area than a horizontal one, so the same duration reads as much
    faster; raise this if the slide is hard to follow.
- showDurationMs: 0
  $name: Reveal animation duration (ms)
  $description: >-
    0 keeps the Windows default (200 ms).
- revealTolerance: 4
  $name: Reveal trigger width (pixels)
  $description: >-
    How close to the screen edge the pointer must get before the taskbar
    reappears.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <shellapi.h>

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

#define WATCH_TIMER_ID 0x57484148
#define WATCH_TICK_MS  120
#define LEAVE_TICKS    2

// ------------------------------------------------------------------ settings

struct Settings {
    bool edgeLeft = true;
    bool edgeTop = true;
    bool edgeRight = true;
    int revealTolerance = 4;
    int hideDurationMs = 0;
    int showDurationMs = 0;
} g_settings;

void LoadSettings() {
    g_settings.edgeLeft = Wh_GetIntSetting(L"edgeLeft") != 0;
    g_settings.edgeTop = Wh_GetIntSetting(L"edgeTop") != 0;
    g_settings.edgeRight = Wh_GetIntSetting(L"edgeRight") != 0;
    g_settings.revealTolerance = Wh_GetIntSetting(L"revealTolerance");
    if (g_settings.revealTolerance < 1 || g_settings.revealTolerance > 64) {
        g_settings.revealTolerance = 4;
    }

    g_settings.hideDurationMs = Wh_GetIntSetting(L"hideDurationMs");
    g_settings.showDurationMs = Wh_GetIntSetting(L"showDurationMs");
    if (g_settings.hideDurationMs < 0 || g_settings.hideDurationMs > 5000) {
        g_settings.hideDurationMs = 0;
    }
    if (g_settings.showDurationMs < 0 || g_settings.showDurationMs > 5000) {
        g_settings.showDurationMs = 0;
    }
}

bool EdgeEnabled(UINT edge) {
    switch (edge) {
        case ABE_LEFT:  return g_settings.edgeLeft;
        case ABE_TOP:   return g_settings.edgeTop;
        case ABE_RIGHT: return g_settings.edgeRight;
        default:        return false;   // bottom works natively
    }
}

PCWSTR EdgeName(UINT edge) {
    switch (edge) {
        case ABE_LEFT:   return L"left";
        case ABE_TOP:    return L"top";
        case ABE_RIGHT:  return L"right";
        case ABE_BOTTOM: return L"bottom";
        default:         return L"?";
    }
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
bool g_timerSet = false;
int g_leaveTicks = 0;
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
        default:        return false;
    }
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

void ApplyDurationOverrides(void* pThis) {
    if (!g_durationOffsetsVerified || !pThis) {
        return;
    }
    if (g_settings.hideDurationMs > 0) {
        *(int*)((BYTE*)pThis + SLIDE_HIDE_DURATION_OFFSET) =
            g_settings.hideDurationMs;
    }
    if (g_settings.showDurationMs > 0) {
        *(int*)((BYTE*)pThis + SLIDE_SHOW_DURATION_OFFSET) =
            g_settings.showDurationMs;
    }
}

// ------------------------------------------------------------ pointer watcher

// Windows' cursor-at-edge detection does not run off-bottom, and none of the
// hooked functions are called while the taskbar sits idle, so there is no
// polling site. A timer on the taskbar's own thread supplies one.
void WatcherTick() {
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
        if (!CursorAtRevealStrip(edge)) {
            g_leaveTicks = 0;
            return;
        }

        ApplyDurationOverrides(g_trayUI);
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

    if (++g_leaveTicks < LEAVE_TICKS) {
        return;
    }
    g_leaveTicks = 0;

    // Unhide slides itself, but nothing performs the hide slide off-bottom, so
    // set the state and then issue the slide. With the flags now set,
    // GetDockedRect returns the hidden rect.
    g_inWatcherCall = true;
    TrayUI_SetAutoHideFlags_Original(g_trayUI, stored | ABS_ALWAYSONTOP);

    if (TrayUI_SlideWindow_Original && g_trayWnd) {
        RECT hidden{};
        TrayUI_GetDockedRect_Original(g_trayUI, &hidden, 0);

        // Apply immediately before the slide: SetAutoHideFlags above resets
        // these members, which is why applying earlier worked for the reveal
        // (nothing intervenes before Unhide) but not for the hide.
        ApplyDurationOverrides(g_trayUI);

        TrayUI_SlideWindow_Original(
            g_trayUI, g_trayWnd, &hidden,
            MonitorFromWindow(g_trayWnd, MONITOR_DEFAULTTOPRIMARY), false,
            true);
    }
    g_inWatcherCall = false;
}

LRESULT CALLBACK TrayWatchProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                               LPARAM lParam, DWORD_PTR dwRefData) {
    if (!g_timerSet) {
        // Set from the window's own thread, which is where messages arrive.
        SetTimer(hWnd, WATCH_TIMER_ID, WATCH_TICK_MS, nullptr);
        g_timerSet = true;
    }

    if (uMsg == WM_TIMER && wParam == WATCH_TIMER_ID) {
        WatcherTick();
        return 0;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
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

    // The watcher needs a TrayUI pointer, which is only captured when
    // GetDockedRect runs, and that only happens on a layout pass. Without a
    // nudge the taskbar stays visible after enabling until something else
    // triggers one.
    if (g_trayWnd) {
        PostMessageW(g_trayWnd, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
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

    if (g_lastDockedRect.right > g_lastDockedRect.left) {
        SetWindowPos(g_trayWnd, nullptr, g_lastDockedRect.left,
                     g_lastDockedRect.top,
                     g_lastDockedRect.right - g_lastDockedRect.left,
                     g_lastDockedRect.bottom - g_lastDockedRect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    PostMessageW(g_trayWnd, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
}

void Wh_ModBeforeUninit() {
    RestoreTaskbarIfHidden();
}

void Wh_ModUninit() {
    if (g_trayWnd && g_timerSet) {
        KillTimer(g_trayWnd, WATCH_TIMER_ID);
    }
    if (g_trayWnd && g_subclassed) {
        WindhawkUtils::RemoveWindowSubclassFromAnyThread(g_trayWnd,
                                                         TrayWatchProc);
    }
}
