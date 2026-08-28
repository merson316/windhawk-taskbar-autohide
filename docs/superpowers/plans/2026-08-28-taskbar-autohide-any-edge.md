# Taskbar Auto-Hide On Any Edge — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make "Automatically hide the taskbar" work on the top, left and right edges in Windows 11 26H2, via a Windhawk mod.

**Architecture:** Windows 11 keeps its tray logic in `Taskbar.dll` as class `TrayUI`, whose auto-hide machinery is already parameterised by edge. The observed symptom is that the taskbar never hides at all off-bottom, so the hide decision is being refused before any geometry runs. Phase 1 ships a strictly read-only mod that logs the auto-hide call chain to a file, identifying which link refuses. Phase 2 then overrides only that link. Phase 2 is deliberately not specified here — see Task 8.

**Tech Stack:** C++23, Windhawk 1.7.3 mod API (`windhawk_api.h`, `windhawk_utils.h`), clang++ targeting `x86_64-w64-mingw32` (shipped inside Windhawk), Win32 API. Host tooling is bash under WSL2 driving Windows binaries through `/mnt/c`.

**Spec:** `docs/superpowers/specs/2026-08-28-taskbar-autohide-any-edge-design.md`

## Global Constraints

Every task's requirements implicitly include these. Values are copied verbatim from the spec.

- Target Windows build: **10.0.26300.9278 (26H2)**. Mods must no-op unless build number **>= 26200**.
- Mod injection target is **`explorer.exe` only**. No other process.
- **No global or low-level input hooks** — `WH_MOUSE_LL`, `WH_KEYBOARD_LL`, or any system-wide `SetWindowsHookEx` are forbidden. Cursor work uses in-process `GetCursorPos` polling.
- **No injection, memory access or handle acquisition against any process other than `explorer.exe`.**
- Phase 1 must not alter behaviour: every hook calls the original and returns its result unmodified.
- Symbol hook strings are the **mangled names taken verbatim** from this machine's cached PDB. Adjustor thunks (`W7EAA`, `WBI@EAA` variants) must not be hooked.
- Diagnostic log path: `%LOCALAPPDATA%\windhawk-autohide-diag.log`, written line-at-a-time so an Explorer restart cannot lose the trace.
- The live taskbar edge comes from `TrayUI::GetStuckPlace()`. **Never read `StuckRects3`** — it holds a stale right-edge blob on this machine.
- Secondary displays are out of scope while `MMTaskbarEnabled` is `0`.
- Escape hatch if Explorer wedges: `windhawk.exe -safe-mode`.

## Verification Model

This project has no unit-test framework and cannot have one: the code under test is a DLL injected into `explorer.exe`, and its behaviour is observed through the Windows shell. Classic red-green TDD does not map. The substitute cycle, used in every task below, is:

1. **Write the check first** — an executable command or an explicit observable criterion.
2. **Run it and watch it fail** — the value is absent, the file does not exist, the log lacks the marker.
3. **Implement.**
4. **Run the check again and watch it pass.**
5. **Commit.**

Two of these checks are objective and scriptable (compilation, log contents). The runtime shell behaviour checks are manual and are marked **[MANUAL]** — they require the human operator at the machine.

## File Structure

| Path | Responsibility |
|---|---|
| `tools/whcompile.sh` | Pre-flight compile of a `.wh.cpp` using Windhawk's own clang. Objective pass/fail gate before any code reaches the Windhawk editor. |
| `tools/readlog.sh` | Read/tail/clear the diagnostic log from `%LOCALAPPDATA%` via `/mnt/c`. |
| `src/taskbar-autohide-any-edge-diag.wh.cpp` | Phase 1. Read-only. Logging infrastructure plus the instrumented hooks. |
| `docs/windhawk-config.md` | Records the injection exclusion configuration and how to verify it. |
| `docs/findings-phase1.md` | The diagnosis produced by Task 7. Input to phase 2 planning. |

`src/taskbar-autohide-any-edge.wh.cpp` (phase 2) is created by a later plan, not this one.

---

### Task 1: Windhawk game-path injection exclusions

Applied before any mod is enabled, so that later mod breakage is attributable to the mod rather than to this configuration change.

**Files:**
- Create: `docs/windhawk-config.md`

**Interfaces:**
- Consumes: nothing.
- Produces: a verified Windhawk configuration. No code artifacts.

- [ ] **Step 1: Write the verification command and run it — expect failure**

```bash
/mnt/c/Windows/System32/reg.exe query "HKLM\SOFTWARE\Windhawk\Engine\Settings" \
  /v Exclude 2>&1 | tr -d '\r' 
```

Expected now: **no output** (no exclusion value has been set). This is the failing state.

- [ ] **Step 2: Apply the configuration [MANUAL]**

In Windhawk: **Settings → Advanced settings → More advanced settings → Process exclusion list**. Add these six lines, keeping any entries already present:

```
<GameDrive>:\SteamLibrary\*
<GameDrive>:\SteamLibrary\*
<GameDrive>:\SteamLibrary\*
<GameDrive>:\Origin\*
<GameDrive>:\BNet\*
C:\XboxGames\*
```

Save, and let Windhawk restart its engine when prompted.

- [ ] **Step 3: Re-run the verification command — expect success**

```bash
/mnt/c/Windows/System32/reg.exe query "HKLM\SOFTWARE\Windhawk\Engine\Settings" \
  /v Exclude 2>&1 | tr -d '\r' 
```

Expected: a value is present containing the six paths. If the value name differs from a guessed name, that is fine — the check is that the paths appear.

- [ ] **Step 4: Confirm existing mods still work [MANUAL]**

The taskbar should still auto-hide-speed normally and Start menu "All apps" should still behave as modded. If either broke, the exclusion list was mis-entered — fix before continuing.

- [ ] **Step 5: Confirm a game still launches [MANUAL]**

Launch one title from an excluded library (any Steam game on `E:`, `F:` or `G:`). It should start normally. This is the concrete outcome the exclusions exist to protect.

- [ ] **Step 6: Record the configuration**

Create `docs/windhawk-config.md`:

```markdown
# Windhawk injection configuration

Windhawk's engine injects into all processes by default, minus built-in
exclusions. A mod's `@include` controls where the *mod* runs, not where the
*engine* is injected, so per-mod targeting does not protect games.

## Applied configuration

Settings -> Advanced settings -> More advanced settings -> Process exclusion
list, additive to Windhawk's built-in exclusions:

    <GameDrive>:\SteamLibrary\*
    <GameDrive>:\SteamLibrary\*
    <GameDrive>:\SteamLibrary\*
    <GameDrive>:\Origin\*
    <GameDrive>:\BNet\*
    C:\XboxGames\*

## Verify

    /mnt/c/Windows/System32/reg.exe query \
      "HKLM\SOFTWARE\Windhawk\Engine\Settings" /v Exclude | tr -d '\r' 

## Maintenance

Adding a new game library root means adding one more line here. Games installed
inside the roots above are covered automatically.

## Rejected alternative

Excluding `*` and allowlisting only `explorer.exe`, `<other-app>.exe` and
`StartMenuExperienceHost.exe` is more contained, but every future mod install
would silently do nothing until its targets were added by hand, and blanket
exclusion delays injection in a way that may break some mods.
```

- [ ] **Step 7: Commit**

```bash
cd <repo>
git add docs/windhawk-config.md
git commit -m "docs: record Windhawk game-path injection exclusions"
```

---

### Task 2: Host tooling — pre-flight compile and log reader

**Files:**
- Create: `tools/whcompile.sh`
- Create: `tools/readlog.sh`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `tools/whcompile.sh <path-to.wh.cpp>` — exit `0` on clean compile, non-zero on error; diagnostics on stdout.
  - `tools/readlog.sh [--clear|--tail]` — prints the diagnostic log; `--clear` truncates it; `--tail` follows.

- [ ] **Step 1: Write the check and run it — expect failure**

```bash
cd <repo>
tools/whcompile.sh research/reference-taskbar-auto-hide-speed.wh.cpp; echo "EXIT=$?"
```

Expected: `bash: tools/whcompile.sh: No such file or directory`, EXIT non-zero.

- [ ] **Step 2: Implement `tools/whcompile.sh`**

```bash
#!/usr/bin/env bash
# Pre-flight compile a Windhawk mod source using Windhawk's own clang.
# Usage: tools/whcompile.sh <file.wh.cpp>
set -uo pipefail

SRC="${1:?usage: whcompile.sh <file.wh.cpp>}"
[ -f "$SRC" ] || { echo "no such file: $SRC" >&2; exit 2; }

CLANG="/mnt/c/Program Files/Windhawk/Compiler/bin/clang++.exe"
INCLUDE='C:\Program Files\Windhawk\Compiler\include'
[ -x "$CLANG" ] || { echo "Windhawk clang not found at $CLANG" >&2; exit 2; }

# The compiler is a Windows binary, so the source must sit on a Windows volume.
STAGE_WIN='C:\Users\<user>\AppData\Local\Temp'
STAGE_WSL='/mnt/c/Users/<user>/AppData/Local/Temp'
BASE="whcompile_$$.cpp"
cp "$SRC" "$STAGE_WSL/$BASE" || exit 2
trap 'rm -f "$STAGE_WSL/$BASE"' EXIT

"$CLANG" -fsyntax-only \
  -x c++ -std=c++23 -target x86_64-w64-mingw32 \
  -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 \
  -D_WIN32_IE=0x0A00 -DNTDDI_VERSION=0x0A000008 \
  -D__USE_MINGW_ANSI_STDIO=0 -DWH_MOD -DWH_EDITING \
  -I "$INCLUDE" -include windhawk_api.h \
  -Wno-pragma-pack -Wno-pragma-system-header-outside-header \
  "$STAGE_WIN\\$BASE" 2>&1 | sed "s|[^ ]*$BASE|$SRC|g"

exit "${PIPESTATUS[0]}"
```

Make it executable:

```bash
chmod +x tools/whcompile.sh
```

- [ ] **Step 3: Run the positive check — expect pass**

```bash
tools/whcompile.sh research/reference-taskbar-auto-hide-speed.wh.cpp; echo "EXIT=$?"
```

Expected: EXIT=0. Deprecation warnings about `Wh_SetFunctionHookT` are expected and harmless — they come from the reference mod, not from our code.

- [ ] **Step 4: Run the negative check — expect a detected failure**

A compile gate that cannot fail is worthless, so prove it rejects bad input:

```bash
printf 'int main() { this is not c++ }\n' > /tmp/broken.wh.cpp
tools/whcompile.sh /tmp/broken.wh.cpp; echo "EXIT=$?"
```

Expected: compiler errors printed, EXIT non-zero.

- [ ] **Step 5: Implement `tools/readlog.sh`**

```bash
#!/usr/bin/env bash
# Read the phase 1 diagnostic log.
# Usage: tools/readlog.sh [--clear|--tail]
set -uo pipefail

LOG="/mnt/c/Users/<user>/AppData/Local/windhawk-autohide-diag.log"

case "${1:-}" in
  --clear) : > "$LOG" 2>/dev/null && echo "cleared $LOG" || echo "not present: $LOG"; ;;
  --tail)  tail -f "$LOG" ;;
  "")      if [ -f "$LOG" ]; then cat "$LOG"; else echo "not present: $LOG" >&2; exit 1; fi ;;
  *)       echo "usage: readlog.sh [--clear|--tail]" >&2; exit 2 ;;
esac
```

```bash
chmod +x tools/readlog.sh
```

- [ ] **Step 6: Verify the log reader reports absence cleanly**

```bash
tools/readlog.sh; echo "EXIT=$?"
```

Expected: `not present: /mnt/c/Users/<user>/AppData/Local/windhawk-autohide-diag.log`, EXIT=1. The mod has not run yet, so absence is correct.

- [ ] **Step 7: Commit**

```bash
git add tools/whcompile.sh tools/readlog.sh
git commit -m "tools: add pre-flight compile and diagnostic log reader"
```

---

### Task 3: Diagnostic mod skeleton — metadata, version guard, logging

Produces a mod that loads into Explorer, writes a log header, and hooks nothing. This proves the plumbing before any hook touches the shell.

**Files:**
- Create: `src/taskbar-autohide-any-edge-diag.wh.cpp`

**Interfaces:**
- Consumes: `tools/whcompile.sh`, `tools/readlog.sh` from Task 2.
- Produces, for Tasks 4 and 5:
  - `void DiagLog(PCWSTR fmt, ...)` — appends one timestamped, thread-tagged line.
  - `HMODULE g_taskbarModule` — handle to `taskbar.dll`, loaded in `Wh_ModInit`.
  - `bool IsSupportedBuild()` — build number gate.

- [ ] **Step 1: Write the check — expect failure**

```bash
cd <repo>
tools/whcompile.sh src/taskbar-autohide-any-edge-diag.wh.cpp; echo "EXIT=$?"
```

Expected: `no such file`, EXIT=2.

- [ ] **Step 2: Create the mod file**

```cpp
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
```

- [ ] **Step 3: Run the compile check — expect pass**

```bash
tools/whcompile.sh src/taskbar-autohide-any-edge-diag.wh.cpp; echo "EXIT=$?"
```

Expected: EXIT=0, no errors.

- [ ] **Step 4: Install and enable the mod [MANUAL]**

In Windhawk: **Create a new mod**, replace the editor contents with the file, compile, and enable it. Explorer will reload the mod automatically.

- [ ] **Step 5: Verify the log appears**

```bash
tools/readlog.sh
```

Expected: at least
```
HH:MM:SS.mmm [t nnnn] === diag mod init, build 26300, pid nnnn ===
HH:MM:SS.mmm [t nnnn] taskbar.dll loaded at 00007FF...
```

- [ ] **Step 6: Verify no behaviour changed [MANUAL]**

The taskbar must look and behave exactly as before. Nothing is hooked yet, so any change here means something is wrong — stop and investigate rather than continuing.

- [ ] **Step 7: Commit**

```bash
git add src/taskbar-autohide-any-edge-diag.wh.cpp
git commit -m "feat: add phase 1 diagnostic mod skeleton with file logging"
```

---

### Task 4: Instrument the auto-hide state and flags chain

The smaller, lower-risk hook group first. If Explorer is going to be destabilised by hooking `TrayUI`, better to discover it with five hooks than with thirteen.

**Files:**
- Modify: `src/taskbar-autohide-any-edge-diag.wh.cpp`

**Interfaces:**
- Consumes: `DiagLog`, `g_taskbarModule`, `IsSupportedBuild` from Task 3.
- Produces, for Task 5:
  - `TrayUI_GetStuckPlace_t g_pGetStuckPlace` — resolved but **not hooked**, so other hooks can query the live edge without re-entering a hook of their own.
  - `UINT CurrentStuckPlace(void* pThis)` — safe wrapper returning the edge, or `0xFFFFFFFF` if unavailable.

- [ ] **Step 1: Write the check — expect failure**

```bash
tools/readlog.sh --clear
```

Then, after toggling auto-hide on the machine, the log must contain a `SetAutoHideState` line. It currently cannot, because nothing is hooked. That absence is the failing state.

- [ ] **Step 2: Add the edge accessor and the state/flags hooks**

Insert directly above `// ------------------------------------------------------------------ lifecycle`:

```cpp
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
```

- [ ] **Step 3: Register the hooks in `Wh_ModInit`**

Replace the `return TRUE;` at the end of `Wh_ModInit` with:

```cpp
    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        // Resolved only; deliberately not hooked.
        {{LR"(?GetStuckPlace@TrayUI@@UEAAIXZ)"}, &g_pGetStuckPlace, nullptr,
         true},

        {{LR"(?GetAutoHideFlags@TrayUI@@UEAAIXZ)"},
         &TrayUI_GetAutoHideFlags_Original, TrayUI_GetAutoHideFlags_Hook, true},

        {{LR"(?GetDesktopModeAutoHide@TrayUI@@UEBAIXZ)"},
         &TrayUI_GetDesktopModeAutoHide_Original,
         TrayUI_GetDesktopModeAutoHide_Hook, true},

        {{LR"(?SetAutoHideFlags@TrayUI@@UEAAXI@Z)"},
         &TrayUI_SetAutoHideFlags_Original, TrayUI_SetAutoHideFlags_Hook, true},

        {{LR"(?SetAutoHideState@TrayUI@@UEAAJH@Z)"},
         &TrayUI_SetAutoHideState_Original, TrayUI_SetAutoHideState_Hook, true},

        {{LR"(?AutoHideChangedOnMonitor@TrayUI@@QEAAXPEAUHMONITOR__@@@Z)"},
         &TrayUI_AutoHideChangedOnMonitor_Original,
         TrayUI_AutoHideChangedOnMonitor_Hook, true},
    };

    if (!WindhawkUtils::HookSymbols(g_taskbarModule, hooks, ARRAYSIZE(hooks))) {
        DiagLog(L"FATAL: HookSymbols failed");
        return FALSE;
    }

    // Every entry is optional so a single rename degrades instead of killing
    // the mod, but a diagnosis without these two is meaningless.
    if (!g_pGetStuckPlace) {
        DiagLog(L"WARN: GetStuckPlace unresolved, edge will log as ?");
    }
    if (!TrayUI_SetAutoHideState_Original) {
        DiagLog(L"FATAL: SetAutoHideState unresolved, cannot diagnose");
        return FALSE;
    }

    DiagLog(L"state/flags hooks installed");
    return TRUE;
```

- [ ] **Step 4: Compile**

```bash
tools/whcompile.sh src/taskbar-autohide-any-edge-diag.wh.cpp; echo "EXIT=$?"
```

Expected: EXIT=0.

- [ ] **Step 5: Update the mod in Windhawk and clear the log [MANUAL + command]**

Paste the updated source into the Windhawk editor, compile, save.

```bash
tools/readlog.sh --clear
```

- [ ] **Step 6: Toggle auto-hide and check the log [MANUAL + command]**

With the taskbar docked left, toggle "Automatically hide the taskbar" on, wait 10 seconds, then off.

```bash
tools/readlog.sh
```

Expected: `state/flags hooks installed`, followed by `SetAutoHideState` and/or `GetAutoHideFlags` lines showing `[edge=0 LEFT]`. If **no** lines appear when toggling, that is itself the finding — spec case 5, the gate is upstream of `TrayUI`.

- [ ] **Step 7: Verify the taskbar still behaves identically [MANUAL]**

Hooks are log-only. Any behaviour change means a signature is wrong — disable the mod and re-check the typedefs before continuing.

- [ ] **Step 8: Commit**

```bash
git add src/taskbar-autohide-any-edge-diag.wh.cpp
git commit -m "feat: instrument TrayUI auto-hide state and flags chain"
```

---

### Task 5: Instrument the geometry and motion chain

**Files:**
- Modify: `src/taskbar-autohide-any-edge-diag.wh.cpp`

**Interfaces:**
- Consumes: `DiagLog`, `CurrentStuckPlace`, `EdgeName`, `g_taskbarModule` from Tasks 3 and 4.
- Produces: no new symbols for later tasks; this completes phase 1 instrumentation.

- [ ] **Step 1: Write the check — expect failure**

After clearing the log and toggling auto-hide, the log must contain either a `SlideWindow` line or an explicit absence of one. Right now neither `SlideWindow` nor `_ComputeHiddenRect` can appear. That is the failing state.

- [ ] **Step 2: Add the geometry and motion hooks**

Insert directly above `// ------------------------------------------------------------------ lifecycle`:

```cpp
// ----------------------------------------------------- geometry/motion hooks

using TrayUI_ComputeHiddenRect_t = void(WINAPI*)(void* pThis, RECT* rect,
                                                 UINT edge);
TrayUI_ComputeHiddenRect_t TrayUI_ComputeHiddenRect_Original;
void WINAPI TrayUI_ComputeHiddenRect_Hook(void* pThis, RECT* rect, UINT edge) {
    DiagLog(L"_ComputeHiddenRect(edge=%u %s) ENTER", edge, EdgeName(edge));
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
    DiagLog(L"SlideWindow show=%d animate=%d [edge=%u %s] rect=(%ld,%ld)-(%ld,%ld)",
            (int)show, (int)animate, edge, EdgeName(edge),
            rect ? rect->left : 0, rect ? rect->top : 0,
            rect ? rect->right : 0, rect ? rect->bottom : 0);
    TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, show, animate);
    DiagLog(L"SlideWindow LEAVE show=%d", (int)show);
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
```

- [ ] **Step 3: Add these entries to the `hooks[]` array in `Wh_ModInit`**

Insert before the closing `};` of the existing array:

```cpp
        {{LR"(?_ComputeHiddenRect@TrayUI@@QEAAXPEAUtagRECT@@I@Z)"},
         &TrayUI_ComputeHiddenRect_Original, TrayUI_ComputeHiddenRect_Hook,
         true},

        {{LR"(?GetDockedRect@TrayUI@@UEAAIPEAUtagRECT@@H@Z)"},
         &TrayUI_GetDockedRect_Original, TrayUI_GetDockedRect_Hook, true},

        {{LR"(?ShrinkRectToStuckEdge@TrayUI@@AEBAXAEAUtagRECT@@H@Z)"},
         &TrayUI_ShrinkRectToStuckEdge_Original,
         TrayUI_ShrinkRectToStuckEdge_Hook, true},

        {{LR"(?GetStuckInfo@TrayUI@@UEAAXPEAUtagRECT@@PEAI@Z)"},
         &TrayUI_GetStuckInfo_Original, TrayUI_GetStuckInfo_Hook, true},

        {{LR"(?SlideWindow@TrayUI@@UEAAXPEAUHWND__@@PEBUtagRECT@@PEAUHMONITOR__@@_N3@Z)"},
         &TrayUI_SlideWindow_Original, TrayUI_SlideWindow_Hook, true},

        {{LR"(?Unhide@TrayUI@@UEAAXW4TrayUnhideFlags@TrayCommon@@W4UnhideRequest@3@@Z)"},
         &TrayUI_Unhide_Original, TrayUI_Unhide_Hook, true},

        {{LR"(?SetUnhideTimer@TrayUI@@UEAAXJJ@Z)"},
         &TrayUI_SetUnhideTimer_Original, TrayUI_SetUnhideTimer_Hook, true},

        {{LR"(?_AutoHideCollision@TrayUI@@QEAAXXZ)"},
         &TrayUI_AutoHideCollision_Original, TrayUI_AutoHideCollision_Hook,
         true},
```

Then change the log line at the end of `Wh_ModInit` from `state/flags hooks installed` to:

```cpp
    DiagLog(L"all hooks installed (SlideWindow=%p, ComputeHiddenRect=%p)",
            (void*)TrayUI_SlideWindow_Original,
            (void*)TrayUI_ComputeHiddenRect_Original);
```

- [ ] **Step 4: Compile**

```bash
tools/whcompile.sh src/taskbar-autohide-any-edge-diag.wh.cpp; echo "EXIT=$?"
```

Expected: EXIT=0.

- [ ] **Step 5: Update in Windhawk, clear the log [MANUAL + command]**

```bash
tools/readlog.sh --clear
```

- [ ] **Step 6: Sanity-check that instrumentation is live [MANUAL + command]**

Auto-hide still off, simply click the taskbar and open Start.

```bash
tools/readlog.sh | head -30
```

Expected: the `all hooks installed` line with two non-null pointers. `GetDockedRect` / `GetStuckInfo` lines are likely to appear from routine shell activity, confirming hooks fire.

- [ ] **Step 7: Verify behaviour is still unchanged [MANUAL]**

Taskbar identical to stock. `SlideWindow` is on the animation path, so if the taskbar visibly stutters, stop — the signature is wrong.

- [ ] **Step 8: Commit**

```bash
git add src/taskbar-autohide-any-edge-diag.wh.cpp
git commit -m "feat: instrument TrayUI geometry and motion chain"
```

---

### Task 6: Capture the diagnostic trace and identify the gate

The deliverable is a diagnosis, not code.

**Files:**
- Create: `docs/findings-phase1.md`

**Interfaces:**
- Consumes: the fully instrumented mod from Task 5.
- Produces: `docs/findings-phase1.md`, naming which spec case applies. Sole input to phase 2 planning.

- [ ] **Step 1: Clear the log**

```bash
cd <repo>
tools/readlog.sh --clear
```

- [ ] **Step 2: Run the scenario [MANUAL]**

With the taskbar docked **left** and the diagnostic mod enabled, in order:

1. Toggle "Automatically hide the taskbar" **on**.
2. Wait ~10 s, cursor away from the left edge and away from the taskbar.
3. Sweep the cursor into the left edge and back out. Twice.
4. Click the taskbar. Open Start, then close it.
5. Toggle the setting **off**.

- [ ] **Step 3: Capture the trace**

```bash
tools/readlog.sh > /tmp/phase1-left.log
wc -l /tmp/phase1-left.log
grep -cE "SetAutoHideState|SlideWindow|_ComputeHiddenRect|Unhide" /tmp/phase1-left.log
```

- [ ] **Step 4: Repeat for the bottom edge — the control**

This is the step that makes the trace interpretable. Without a known-good trace there is nothing to diff against.

```bash
tools/readlog.sh --clear
```

Move the taskbar to the **bottom** [MANUAL], repeat Step 2's scenario (auto-hide works there), then:

```bash
tools/readlog.sh > /tmp/phase1-bottom.log
wc -l /tmp/phase1-bottom.log
```

- [ ] **Step 5: Diff the two traces**

```bash
cut -d']' -f2- /tmp/phase1-bottom.log | sed 's/[0-9]\+/N/g' | sort -u > /tmp/b.norm
cut -d']' -f2- /tmp/phase1-left.log   | sed 's/[0-9]\+/N/g' | sort -u > /tmp/l.norm
echo "=== present at BOTTOM but missing at LEFT ==="
comm -23 /tmp/b.norm /tmp/l.norm
```

The first call present at bottom and absent at left is the gate.

- [ ] **Step 6: Restore the taskbar to the left edge [MANUAL]**

- [ ] **Step 7: Write the findings**

Create `docs/findings-phase1.md` recording: the two traces' line counts, the diff output, the identified gate function, and **which of the spec's five phase-2 cases applies**. Quote the decisive log lines verbatim. If the answer is case 5 (nothing in the chain fires), state that plainly and note that a second diagnostic pass against `Taskbar.View.dll` is required.

- [ ] **Step 8: Commit**

```bash
git add docs/findings-phase1.md
git commit -m "docs: record phase 1 diagnostic findings"
```

---

### Task 7: Phase 2 planning gate

**This task deliberately contains no implementation steps.**

Phase 2 overrides the gate found in Task 6. Which override is correct depends entirely on data that does not exist until Task 6 completes, and the spec enumerates five materially different outcomes. Writing phase 2 code now would mean inventing four implementations that get discarded, and the fifth would be guesswork.

- [ ] **Step 1: Re-enter planning with the findings**

Invoke the `superpowers:brainstorming` skill with `docs/findings-phase1.md` as input, classify the phase 2 change, and produce a second plan at `docs/superpowers/plans/YYYY-MM-DD-taskbar-autohide-any-edge-phase2.md`.

Decision table, from the spec:

| Finding | Phase 2 approach |
|---|---|
| A flag getter returns 0 off-bottom | Hook it to return the enabled value |
| An early-out compares the stuck place | Spoof `GetStuckPlace` filtered by return address, that caller only |
| Hide runs but the rect is wrong | Correct `_ComputeHiddenRect` per edge |
| Hide works, unhide-on-hover does not | Supply the trigger in-process, `GetCursorPos` polling only — never `WH_MOUSE_LL` |
| Nothing in the chain fires | Gate is upstream; second diagnostic pass against `Taskbar.View.dll` |

- [ ] **Step 2: Disable the diagnostic mod [MANUAL]**

It is instrumentation, not a shipping component. Leave it installed but disabled so it can be re-enabled if phase 2 needs another trace.

## Phase 2 acceptance criteria

Carried from the spec so the follow-up plan inherits them. For each of left, top and right:

- Taskbar hides after the idle delay.
- Cursor at the edge reveals it; leaving re-hides it.
- Stays visible while the taskbar has focus or Start is open.
- Work-area reservation is released when auto-hide engages, returning the 48 px to applications.
- A fullscreen application does not force the taskbar over it.
- A flashing taskbar button behaves sanely.
- With the exclusion configuration applied, all existing mods still work.
- Disabling the mod restores stock behaviour and never strands the taskbar hidden.
