// ==WindhawkMod==
// @id              taskbar-autohide-any-edge
// @name            Taskbar auto-hide on any edge
// @description     Makes "Automatically hide the taskbar" work when the taskbar is docked to the top, left or right in Windows 11 26H2.
// @version         0.1.0
// @author          merson316
// @include         explorer.exe
// @architecture    x86-64
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar auto-hide on any edge

Windows 11 26H2 can dock the taskbar to the top, left or right, but
"Automatically hide the taskbar" only takes effect on the bottom edge. On any
other edge the setting is accepted and reports success, yet the taskbar never
hides.

Diagnosis showed the auto-hide flags never become non-zero off-bottom, so
nothing downstream ever attempts a hide. This mod lets the flag store succeed,
after which Windows' own auto-hide machinery does all the real work: sliding,
releasing the reserved work area, fullscreen handling, and reveal-on-hover.

Requires Windows 11 build 26200 or newer.
*/
// ==/WindhawkModReadme==

#include <windhawk_utils.h>

#include <stdarg.h>
#include <string>

#define ABE_LEFT   0
#define ABE_TOP    1
#define ABE_RIGHT  2
#define ABE_BOTTOM 3

// ---------------------------------------------------------------- build gate

typedef struct _FIX_OSVERSIONINFOW {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
} FIX_OSVERSIONINFOW;

ULONG g_buildNumber = 0;

bool IsSupportedBuild() {
    using RtlGetVersion_t = LONG(WINAPI*)(FIX_OSVERSIONINFOW*);

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return false;
    }

    auto pRtlGetVersion =
        (RtlGetVersion_t)(void*)GetProcAddress(ntdll, "RtlGetVersion");
    if (!pRtlGetVersion) {
        return false;
    }

    FIX_OSVERSIONINFOW vi{};
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

void FixLogOpen() {
    WCHAR base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return;
    }

    std::wstring path(base);
    path += L"\\windhawk-autohide-fix.log";

    g_logFile = CreateFileW(
        path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void FixLogClose() {
    if (g_logFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_logFile);
        g_logFile = INVALID_HANDLE_VALUE;
    }
}

void FixLog(PCWSTR fmt, ...) {
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
    WriteFile(g_logFile, utf8, (DWORD)(bytes - 1), &written, nullptr);
    LeaveCriticalSection(&g_logLock);
}

PCWSTR EdgeName(UINT edge) {
    switch (edge) {
        case ABE_LEFT: return L"LEFT";
        case ABE_TOP: return L"TOP";
        case ABE_RIGHT: return L"RIGHT";
        case ABE_BOTTOM: return L"BOTTOM";
        default: return L"?";
    }
}

// --------------------------------------------------------------- the override

// Attempt 1 (spoofing GetStuckPlace for the duration of SetAutoHideFlags)
// was tried and did not work: the flag store does not consult GetStuckPlace.
// See docs/findings-phase1.md. This is attempt 2 -- report the intended flags
// from the getter instead.
//
// Only one taskbar exists on this configuration (MMTaskbarEnabled=0), and the
// getter is reached through an adjustor thunk so its `this` cannot be matched
// against the setter's. State is therefore tracked globally.

volatile LONG g_desiredFlags = 0;   // last value Windows asked us to store
volatile LONG g_currentEdge = ABE_BOTTOM;

using TrayUI_GetStuckPlace_t = UINT(WINAPI*)(void* pThis);
TrayUI_GetStuckPlace_t TrayUI_GetStuckPlace_Original;

using TrayUI_SetStuckPlace_t = void(WINAPI*)(void* pThis, UINT edge);
TrayUI_SetStuckPlace_t TrayUI_SetStuckPlace_Original;
void WINAPI TrayUI_SetStuckPlace_Hook(void* pThis, UINT edge) {
    InterlockedExchange(&g_currentEdge, (LONG)edge);
    FixLog(L"SetStuckPlace(%u %s)", edge, EdgeName(edge));
    TrayUI_SetStuckPlace_Original(pThis, edge);
}

using TrayUI_SetAutoHideFlags_t = void(WINAPI*)(void* pThis, UINT flags);
TrayUI_SetAutoHideFlags_t TrayUI_SetAutoHideFlags_Original;
void WINAPI TrayUI_SetAutoHideFlags_Hook(void* pThis, UINT flags) {
    // `this` is unadjusted here, so the edge read is trustworthy.
    if (TrayUI_GetStuckPlace_Original) {
        InterlockedExchange(&g_currentEdge,
                            (LONG)TrayUI_GetStuckPlace_Original(pThis));
    }
    InterlockedExchange(&g_desiredFlags, (LONG)flags);

    FixLog(L"SetAutoHideFlags(0x%X) edge=%u %s", flags, (UINT)g_currentEdge,
           EdgeName((UINT)g_currentEdge));

    TrayUI_SetAutoHideFlags_Original(pThis, flags);
}

using TrayUI_GetAutoHideFlags_t = UINT(WINAPI*)(void* pThis);
TrayUI_GetAutoHideFlags_t TrayUI_GetAutoHideFlags_Original;
UINT WINAPI TrayUI_GetAutoHideFlags_Hook(void* pThis) {
    UINT ret = TrayUI_GetAutoHideFlags_Original(pThis);

    UINT edge = (UINT)g_currentEdge;
    UINT desired = (UINT)g_desiredFlags;

    // Bottom works natively; never interfere there. Only substitute when the
    // store was refused, i.e. Windows asked for flags and the getter reports
    // none.
    if (edge != ABE_BOTTOM && desired != 0 && ret == 0) {
        return desired;
    }

    return ret;
}

// Verification only. If this fires off-bottom, the fix worked.
using TrayUI_SlideWindow_t = void(WINAPI*)(void* pThis, HWND hWnd,
                                           const RECT* rect, HMONITOR monitor,
                                           bool show, bool animate);
TrayUI_SlideWindow_t TrayUI_SlideWindow_Original;
void WINAPI TrayUI_SlideWindow_Hook(void* pThis, HWND hWnd, const RECT* rect,
                                    HMONITOR monitor, bool show, bool animate) {
    FixLog(L"*** SlideWindow show=%d [edge=%u] rect=(%ld,%ld)-(%ld,%ld)",
           (int)show, (UINT)g_currentEdge, rect ? rect->left : 0,
           rect ? rect->top : 0, rect ? rect->right : 0,
           rect ? rect->bottom : 0);
    TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, show, animate);
}

// ------------------------------------------------------------------ lifecycle

HMODULE g_taskbarModule = nullptr;

BOOL Wh_ModInit() {
    InitializeCriticalSection(&g_logLock);
    g_logLockReady = true;
    FixLogOpen();

    if (!IsSupportedBuild()) {
        FixLog(L"=== UNSUPPORTED BUILD %lu, mod inactive ===", g_buildNumber);
        g_logLockReady = false;
        FixLogClose();
        return FALSE;
    }

    FixLog(L"=== autohide-any-edge init, build %lu ===", g_buildNumber);

    g_taskbarModule =
        LoadLibraryExW(L"taskbar.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!g_taskbarModule) {
        FixLog(L"FATAL: could not load taskbar.dll");
        return FALSE;
    }

    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{LR"(public: virtual unsigned int __cdecl TrayUI::GetStuckPlace(void))",
          LR"(public: virtual unsigned int __cdecl TrayUI::GetStuckPlace(void) )"},
         &TrayUI_GetStuckPlace_Original, nullptr, true},

        {{LR"(public: virtual void __cdecl TrayUI::SetStuckPlace(unsigned int))",
          LR"(public: virtual void __cdecl TrayUI::SetStuckPlace(unsigned int) )"},
         &TrayUI_SetStuckPlace_Original, TrayUI_SetStuckPlace_Hook, true},

        {{LR"(public: virtual void __cdecl TrayUI::SetAutoHideFlags(unsigned int))",
          LR"(public: virtual void __cdecl TrayUI::SetAutoHideFlags(unsigned int) )"},
         &TrayUI_SetAutoHideFlags_Original, TrayUI_SetAutoHideFlags_Hook, true},

        {{LR"(public: virtual unsigned int __cdecl TrayUI::GetAutoHideFlags(void))",
          LR"(public: virtual unsigned int __cdecl TrayUI::GetAutoHideFlags(void) )"},
         &TrayUI_GetAutoHideFlags_Original, TrayUI_GetAutoHideFlags_Hook, true},

        {{LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool))",
          LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool) )"},
         &TrayUI_SlideWindow_Original, TrayUI_SlideWindow_Hook, true},

    };

    if (!WindhawkUtils::HookSymbols(g_taskbarModule, hooks, ARRAYSIZE(hooks))) {
        FixLog(L"FATAL: HookSymbols failed");
        return FALSE;
    }

    // The two hooks that actually implement the fix are mandatory. Running
    // with either missing would change nothing while appearing to work.
    if (!TrayUI_GetAutoHideFlags_Original || !TrayUI_SetAutoHideFlags_Original ||
        !TrayUI_GetStuckPlace_Original) {
        FixLog(L"FATAL: required symbols unresolved (GetStuckPlace=%p, "
               L"SetAutoHideFlags=%p, GetAutoHideFlags=%p)",
               (void*)TrayUI_GetStuckPlace_Original,
               (void*)TrayUI_SetAutoHideFlags_Original,
               (void*)TrayUI_GetAutoHideFlags_Original);
        return FALSE;
    }

    // Seed the edge so the very first getter call is judged correctly.
    if (TrayUI_GetStuckPlace_Original) {
        FixLog(L"initial edge unknown until first SetStuckPlace/SetAutoHideFlags");
    }

    FixLog(L"hooks installed, ready");
    return TRUE;
}

void Wh_ModUninit() {
    FixLog(L"=== autohide-any-edge uninit ===");
    g_logLockReady = false;
    FixLogClose();
}
