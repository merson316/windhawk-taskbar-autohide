// ==WindhawkMod==
// @id              taskbar-autohide-anim-diag
// @name            Taskbar auto-hide animation (diagnostic)
// @description     Read-only. Logs whether the taskbar collapse/expand animations run, to compare bottom against other edges.
// @version         0.1.0
// @author          merson316
// @github          https://github.com/merson316
// @include         explorer.exe
// @architecture    x86-64
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar auto-hide animation (diagnostic)

Read-only instrumentation. The v1 auto-hide mod moves the taskbar without the
Windows 11 easing, and the hypothesis is that the compositor animations are
simply never invoked off-bottom.

This mod logs each call to the three taskbar animations, with the current edge,
so a bottom-edge run can be compared against a left/top/right one. It changes
no behaviour: every hook calls the original and returns its result untouched.

Writes to `%LOCALAPPDATA%\windhawk-anim-diag.log`.

Disable the "Taskbar auto-hide speed" mod while testing, since it hooks the same
two functions.
*/
// ==/WindhawkModReadme==

#include <windhawk_utils.h>

#include <shellapi.h>
#include <stdarg.h>
#include <string>

// ------------------------------------------------------------------- logging

HANDLE g_logFile = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_logLock;
bool g_logReady = false;

void LogOpen() {
    WCHAR base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return;
    }
    std::wstring path(base);
    path += L"\\windhawk-anim-diag.log";
    g_logFile = CreateFileW(
        path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void Log(PCWSTR fmt, ...) {
    if (g_logFile == INVALID_HANDLE_VALUE || !g_logReady) {
        return;
    }

    WCHAR line[512];
    SYSTEMTIME st;
    GetLocalTime(&st);
    int off = swprintf(line, ARRAYSIZE(line), L"%02u:%02u:%02u.%03u ", st.wHour,
                       st.wMinute, st.wSecond, st.wMilliseconds);
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

    char utf8[1024];
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

// Report the edge alongside each animation, so one log distinguishes a
// bottom-edge run from any other.
UINT CurrentEdge() {
    RECT wa{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) {
        return 0xFFFFFFFF;
    }
    LONG sw = GetSystemMetrics(SM_CXSCREEN);
    LONG sh = GetSystemMetrics(SM_CYSCREEN);
    LONG l = wa.left, t = wa.top, r = sw - wa.right, b = sh - wa.bottom;
    LONG best = l;
    UINT edge = ABE_LEFT;
    if (t > best) { best = t; edge = ABE_TOP; }
    if (r > best) { best = r; edge = ABE_RIGHT; }
    if (b > best) { best = b; edge = ABE_BOTTOM; }
    return best <= 0 ? 0xFFFFFFFF : edge;   // no reservation: already hidden
}

// --------------------------------------------------------------------- hooks

// Returns a large struct, so the ABI passes a hidden return pointer.
using Animation_t = void**(WINAPI*)(void* pThis, void* sret);

Animation_t Collapse_Original;
void** WINAPI Collapse_Hook(void* pThis, void* sret) {
    Log(L"TaskbarCollapseAnimation   [workarea edge=%u]", CurrentEdge());
    return Collapse_Original(pThis, sret);
}

Animation_t CollapseDelayed_Original;
void** WINAPI CollapseDelayed_Hook(void* pThis, void* sret) {
    Log(L"TaskbarCollapseDelayedAnim [workarea edge=%u]", CurrentEdge());
    return CollapseDelayed_Original(pThis, sret);
}

Animation_t Expand_Original;
void** WINAPI Expand_Hook(void* pThis, void* sret) {
    Log(L"TaskbarExpandAnimation     [workarea edge=%u]", CurrentEdge());
    return Expand_Original(pThis, sret);
}

// --------------------------------------------------- SlideWindow durations

// SlideWindow gates its animation on three things:
//
//     test %r14d,%r14d           ; 1. duration > 0
//     jle  <skip>
//     call <vtable+0x360>        ; 2. must return non-zero
//     je   <skip>
//     SystemParametersInfo(SPI_GETMENUANIMATION)
//     je   <skip>                ; 3. system animation setting on
//
// r14d comes from this+0x168 when hiding and this+0x16c when showing. The
// bottom edge animates, so gates 2 and 3 pass; log the durations to see whether
// gate 1 is what differs off-bottom.
#define SLIDE_HIDE_DURATION_OFFSET 0x168
#define SLIDE_SHOW_DURATION_OFFSET 0x16c

using TrayUI_SlideWindow_t = void(WINAPI*)(void* pThis, HWND hWnd,
                                           const RECT* rect, HMONITOR monitor,
                                           bool show, bool animate);
TrayUI_SlideWindow_t SlideWindow_Original;
void WINAPI SlideWindow_Hook(void* pThis, HWND hWnd, const RECT* rect,
                             HMONITOR monitor, bool show, bool animate) {
    int hideMs = pThis ? *(int*)((BYTE*)pThis + SLIDE_HIDE_DURATION_OFFSET) : -1;
    int showMs = pThis ? *(int*)((BYTE*)pThis + SLIDE_SHOW_DURATION_OFFSET) : -1;

    BOOL sysAnim = TRUE;
    SystemParametersInfoW(0x1002 /* SPI_GETMENUANIMATION */, 0, &sysAnim, 0);

    // The animated path interpolates from the window's current rect to the
    // target. Equal rects mean zero distance and an instant, invisible
    // "animation", so log both.
    RECT cur{};
    if (hWnd) {
        GetWindowRect(hWnd, &cur);
    }
    bool sameRect = rect && cur.left == rect->left && cur.top == rect->top &&
                    cur.right == rect->right && cur.bottom == rect->bottom;

    // The remaining animation gate is a virtual call at vtable+0x360 whose zero
    // return skips the animation. Report its address as an RVA so it can be
    // identified in the binary.
    static bool loggedSlot = false;
    if (!loggedSlot && pThis) {
        loggedSlot = true;
        void** vtable = *(void***)pThis;
        void* fn = vtable[0x360 / sizeof(void*)];
        HMODULE tb = GetModuleHandleW(L"taskbar.dll");
        Log(L"animation gate: vtable+0x360 -> %p  (taskbar.dll+0x%X)", fn,
            tb ? (unsigned)((BYTE*)fn - (BYTE*)tb) : 0u);
    }

    Log(L"SlideWindow show=%d animate=%d dur(h=%d,s=%d) sysAnim=%d "
        L"from(%ld,%ld)-(%ld,%ld) to(%ld,%ld)-(%ld,%ld) same=%d",
        (int)show, (int)animate, hideMs, showMs, (int)sysAnim, cur.left,
        cur.top, cur.right, cur.bottom, rect ? rect->left : 0,
        rect ? rect->top : 0, rect ? rect->right : 0, rect ? rect->bottom : 0,
        (int)sameRect);

    SlideWindow_Original(pThis, hWnd, rect, monitor, show, animate);
}

// ------------------------------------------------------------------ lifecycle

BOOL Wh_ModInit() {
    InitializeCriticalSection(&g_logLock);
    g_logReady = true;
    LogOpen();

    HMODULE view = GetModuleHandleW(L"Taskbar.View.dll");
    if (!view) {
        view = GetModuleHandleW(L"ExplorerExtensions.dll");
    }
    if (!view) {
        Log(L"=== Taskbar.View.dll not loaded, cannot instrument ===");
        return FALSE;
    }

    Log(L"=== anim diag init, view module at %p ===", (void*)view);

    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{LR"(public: struct std::pair<struct winrt::Windows::UI::Composition::KeyFrameAnimation,struct winrt::WindowsUdk::UI::Composition::AnimationLastFrameTelemetry> __cdecl winrt::Taskbar::implementation::SharedAnimations::TaskbarCollapseAnimation(void)const )"},
         &Collapse_Original, Collapse_Hook, true},

        {{LR"(public: struct std::pair<struct winrt::Windows::UI::Composition::KeyFrameAnimation,struct winrt::WindowsUdk::UI::Composition::AnimationLastFrameTelemetry> __cdecl winrt::Taskbar::implementation::SharedAnimations::TaskbarCollapseDelayedAnimation(void)const )"},
         &CollapseDelayed_Original, CollapseDelayed_Hook, true},

        {{LR"(public: struct std::pair<struct winrt::Windows::UI::Composition::KeyFrameAnimation,struct winrt::WindowsUdk::UI::Composition::AnimationLastFrameTelemetry> __cdecl winrt::Taskbar::implementation::SharedAnimations::TaskbarExpandAnimation(void)const )"},
         &Expand_Original, Expand_Hook, true},
    };

    if (!WindhawkUtils::HookSymbols(view, hooks, ARRAYSIZE(hooks))) {
        Log(L"HookSymbols failed");
        return FALSE;
    }

    Log(L"anim hooks: collapse=%p collapseDelayed=%p expand=%p",
        (void*)Collapse_Original, (void*)CollapseDelayed_Original,
        (void*)Expand_Original);

    HMODULE taskbar =
        LoadLibraryExW(L"taskbar.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (taskbar) {
        WindhawkUtils::SYMBOL_HOOK slideHook[] = {
            {{LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool))",
              LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool) )"},
             &SlideWindow_Original, SlideWindow_Hook, true},
        };
        WindhawkUtils::HookSymbols(taskbar, slideHook, ARRAYSIZE(slideHook));
        Log(L"slide hook: %p", (void*)SlideWindow_Original);
    }

    return TRUE;
}

void Wh_ModUninit() {
    Log(L"=== anim diag uninit ===");
    g_logReady = false;
    if (g_logFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_logFile);
        g_logFile = INVALID_HANDLE_VALUE;
    }
}
