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
