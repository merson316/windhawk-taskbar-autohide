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
UINT WINAPI TrayUI_GetAutoHideFlags_Hook(void* pThis) {
    UINT ret = TrayUI_GetAutoHideFlags_Original(pThis);

    UINT edge = (UINT)g_currentEdge;
    UINT desired = (UINT)g_desiredFlags;

    if (edge == ABE_BOTTOM) {
        return ret;   // works natively; never interfere
    }

    UINT eff = ret;

    // The store is refused off-bottom, so report what was asked for.
    if (eff == 0 && desired != 0) {
        eff = desired;
    }

    // GetDockedRect computes the hidden rect only when (flags & 3) == 3, i.e.
    // ABS_AUTOHIDE | ABS_ALWAYSONTOP:
    //
    //     call GetAutoHideFlags
    //     and  $0x3,%eax
    //     cmp  $0x3,%al
    //     jne  <return docked rect>
    //     call _ComputeHiddenRect
    //
    // On the bottom edge Windows stores 0x1 and then follows up with 0x3. Off
    // bottom the follow-up never happens and the flags stay 0x1, one bit short
    // of the test. Supply the missing bit.
    if (eff & 1) {
        eff |= 2;
    }

    if (eff != ret) {
        DiagLog(L"GetAutoHideFlags -> 0x%X (real 0x%X, edge %u %s)", eff, ret,
                edge, EdgeName(edge));
    }
    return eff;
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

using TrayUI_SetAutoHideFlags_t = void(WINAPI*)(void* pThis, UINT flags);
TrayUI_SetAutoHideFlags_t TrayUI_SetAutoHideFlags_Original;
void WINAPI TrayUI_SetAutoHideFlags_Hook(void* pThis, UINT flags) {
    UINT edge = CurrentStuckPlace(pThis);
    if (edge <= 3) {
        InterlockedExchange(&g_currentEdge, (LONG)edge);
    }
    InterlockedExchange(&g_desiredFlags, (LONG)flags);

    DiagLog(L"SetAutoHideFlags(0x%X)  ENTER [edge=%u %s]", flags, edge,
            EdgeName(edge));
    TrayUI_SetAutoHideFlags_Original(pThis, flags);
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
UINT WINAPI TrayUI_GetDockedRect_Hook(void* pThis, RECT* rect, int param) {
    UINT ret = TrayUI_GetDockedRect_Original(pThis, rect, param);
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
    TrayUI_Unhide_Original(pThis, flags, request);
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

// ---------------------------------------------------------------- the gate

// Disassembly of TrayUI::SetAutoHideState shows:
//
//     lea   -0x28(%rcx),%r12      ; adjust `this`
//     mov   %r12,%rcx
//     call  sub_18005CFF4         ; predicate
//     xor   %edi,%edi
//     test  %al,%al
//     cmove %ebx,%edi             ; predicate true -> requested state forced 0
//
// so a true return silently disables auto-hide. The predicate itself is:
//
//     add   $0x298,%rcx
//     cmp   (%rcx),0              ; null -> false
//     call  ...                   ; resolve member
//     call  sub_18005D398         ; query
//     cmp   $0x2,%eax             ; == 2 -> true (suppress)
//
// It has no exported symbol, so it is hooked by relative address, guarded by
// a byte signature so that a Windows update which moves or changes it causes
// the hook to be skipped rather than applied to the wrong code.

const DWORD kAutoHideSuppressedRva = 0x5CFF4;

const BYTE kAutoHideSuppressedSig[] = {
    0x48, 0x89, 0x5C, 0x24, 0x18,              // mov  %rbx,0x18(%rsp)
    0x57,                                      // push %rdi
    0x48, 0x83, 0xEC, 0x20,                    // sub  $0x20,%rsp
    0x33, 0xDB,                                // xor  %ebx,%ebx
    0x8B, 0xFB,                                // mov  %ebx,%edi
    0x89, 0x5C, 0x24, 0x30,                    // mov  %ebx,0x30(%rsp)
    0x48, 0x81, 0xC1, 0x98, 0x02, 0x00, 0x00,  // add  $0x298,%rcx
};

using AutoHideSuppressed_t = BYTE(WINAPI*)(void* pThis);
AutoHideSuppressed_t AutoHideSuppressed_Original;
BYTE WINAPI AutoHideSuppressed_Hook(void* pThis) {
    BYTE ret = AutoHideSuppressed_Original(pThis);
    UINT edge = (UINT)g_currentEdge;

    if (ret && edge != ABE_BOTTOM) {
        DiagLog(L"GATE: suppressed=1 at edge %u %s -> overriding to 0", edge,
                EdgeName(edge));
        return 0;
    }

    if (ret) {
        DiagLog(L"GATE: suppressed=1 at BOTTOM, left alone");
    }
    return ret;
}

bool HookAutoHideGate(HMODULE module) {
    BYTE* target = (BYTE*)module + kAutoHideSuppressedRva;

    if (memcmp(target, kAutoHideSuppressedSig,
               sizeof(kAutoHideSuppressedSig)) != 0) {
        DiagLog(L"GATE: signature mismatch at +0x%X, not hooking",
                kAutoHideSuppressedRva);
        return false;
    }

    Wh_SetFunctionHook((void*)target, (void*)AutoHideSuppressed_Hook,
                       (void**)&AutoHideSuppressed_Original);
    DiagLog(L"GATE: hooked at +0x%X", kAutoHideSuppressedRva);
    return true;
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
    HookAutoHideGate(g_taskbarModule);

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
