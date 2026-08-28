// ==WindhawkMod==
// @id              taskbar-autohide-any-edge-diag
// @name            Taskbar auto-hide any edge (diagnostic)
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

Writes to `%LOCALAPPDATA%\windhawk-autohide-diag.log`. Changes no behaviour:
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
    path += L"\\windhawk-autohide-diag.log";

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
        default: return L"?";
    }
}

using TrayUI_GetStuckPlace_t = UINT(WINAPI*)(void* pThis);
TrayUI_GetStuckPlace_t g_pGetStuckPlace = nullptr;

// Resolved but never hooked, so calling it from inside another hook cannot
// re-enter our own instrumentation.
UINT CurrentStuckPlace(void* pThis) {
    if (!g_pGetStuckPlace || !pThis) {
        return 0xFFFFFFFF;
    }
    return g_pGetStuckPlace(pThis);
}

// --------------------------------------------------------- state/flags hooks

using TrayUI_GetAutoHideFlags_t = UINT(WINAPI*)(void* pThis);
TrayUI_GetAutoHideFlags_t TrayUI_GetAutoHideFlags_Original;
UINT WINAPI TrayUI_GetAutoHideFlags_Hook(void* pThis) {
    UINT ret = TrayUI_GetAutoHideFlags_Original(pThis);
    UINT edge = CurrentStuckPlace(pThis);
    DiagLog(L"GetAutoHideFlags -> 0x%X   [edge=%u %s]", ret, edge,
            EdgeName(edge));
    return ret;
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
    DiagLog(L"SetAutoHideFlags(0x%X)  ENTER [edge=%u %s]", flags, edge,
            EdgeName(edge));
    TrayUI_SetAutoHideFlags_Original(pThis, flags);
    DiagLog(L"SetAutoHideFlags(0x%X)  LEAVE", flags);
}

using TrayUI_SetAutoHideState_t = LONG(WINAPI*)(void* pThis, int state);
TrayUI_SetAutoHideState_t TrayUI_SetAutoHideState_Original;
LONG WINAPI TrayUI_SetAutoHideState_Hook(void* pThis, int state) {
    UINT edge = CurrentStuckPlace(pThis);
    DiagLog(L"SetAutoHideState(%d) ENTER [edge=%u %s]", state, edge,
            EdgeName(edge));
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
        {{LR"(public: virtual unsigned int __cdecl TrayUI::GetStuckPlace(void))"}, &g_pGetStuckPlace, nullptr,
         true},

        {{LR"(public: virtual unsigned int __cdecl TrayUI::GetAutoHideFlags(void))"},
         &TrayUI_GetAutoHideFlags_Original, TrayUI_GetAutoHideFlags_Hook, true},

        {{LR"(public: virtual unsigned int __cdecl TrayUI::GetDesktopModeAutoHide(void)const)"},
         &TrayUI_GetDesktopModeAutoHide_Original,
         TrayUI_GetDesktopModeAutoHide_Hook, true},

        {{LR"(public: virtual void __cdecl TrayUI::SetAutoHideFlags(unsigned int))"},
         &TrayUI_SetAutoHideFlags_Original, TrayUI_SetAutoHideFlags_Hook, true},

        {{LR"(public: virtual long __cdecl TrayUI::SetAutoHideState(int))"},
         &TrayUI_SetAutoHideState_Original, TrayUI_SetAutoHideState_Hook, true},

        {{LR"(public: void __cdecl TrayUI::AutoHideChangedOnMonitor(struct HMONITOR__ *))"},
         &TrayUI_AutoHideChangedOnMonitor_Original,
         TrayUI_AutoHideChangedOnMonitor_Hook, true},
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
        {L"GetStuckPlace", (void*)g_pGetStuckPlace},
        {L"GetAutoHideFlags", (void*)TrayUI_GetAutoHideFlags_Original},
        {L"GetDesktopModeAutoHide", (void*)TrayUI_GetDesktopModeAutoHide_Original},
        {L"SetAutoHideFlags", (void*)TrayUI_SetAutoHideFlags_Original},
        {L"SetAutoHideState", (void*)TrayUI_SetAutoHideState_Original},
        {L"AutoHideChangedOnMonitor", (void*)TrayUI_AutoHideChangedOnMonitor_Original},
    };

    int resolvedCount = 0;
    for (const auto& c : checks) {
        DiagLog(L"  symbol %-28s %s", c.name,
                c.resolved ? L"OK" : L"UNRESOLVED");
        if (c.resolved) {
            resolvedCount++;
        }
    }

    DiagLog(L"state/flags hooks installed: %d/%d resolved", resolvedCount,
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
