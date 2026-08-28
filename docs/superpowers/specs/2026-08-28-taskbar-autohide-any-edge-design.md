# Taskbar auto-hide on any edge (Windows 11 26H2) — design

Date: 2026-08-28
Status: approved, not yet implemented

## Problem

Windows 11 26H2 (build 10.0.26300.9278) added the ability to dock the taskbar
to the top, left or right of the screen. "Automatically hide the taskbar" only
works while the taskbar is docked to the bottom. On any other edge the setting
is accepted but has no effect: the taskbar never hides at all.

The goal is a Windhawk mod that makes auto-hide work on the top, left and right
edges.

## Target environment

| Item | Value |
|---|---|
| Windows build | 10.0.26300.9278 (26H2) |
| Windhawk | 1.7.3 |
| Primary display | 3440x1440 at (0,0), taskbar docked left, 48 px |
| Second display | 1440x2560 portrait at (3440,-422), no taskbar |
| `Advanced\TaskbarLocation` | `0` (= `ABE_LEFT`, matches the live work area) |
| `Advanced\MMTaskbarEnabled` | `0` (taskbar on primary display only) |

`StuckRects3` still holds a stale right-edge blob from an earlier session. It is
not the source of truth on this build and the mod must not read it. The live
edge comes from `TrayUI::GetStuckPlace()`.

## Key finding

The tray logic lives in `Taskbar.dll` as class `TrayUI`. Its auto-hide
machinery is already parameterised by edge — `_ComputeHiddenRect` takes an edge
argument, and `ShrinkRectToStuckEdge` is explicitly edge-aware:

    ?_ComputeHiddenRect@TrayUI@@QEAAXPEAUtagRECT@@I@Z
      void TrayUI::_ComputeHiddenRect(struct tagRECT *, unsigned int)

    ?ShrinkRectToStuckEdge@TrayUI@@AEBAXAEAUtagRECT@@H@Z
      void TrayUI::ShrinkRectToStuckEdge(struct tagRECT &, int) const

So this is very probably not a missing feature but a narrow bottom-only
assumption gating an otherwise complete implementation. Combined with the
observed symptom — never hides *at all*, rather than hides incorrectly — the
decision to hide is being refused before any geometry is computed.

## Approach

Approach A: find the gate, neutralise only the gate.

Rejected alternatives:

- **Blanket edge spoof.** Hooking `TrayUI::GetStuckPlace()` to always report
  `ABE_BOTTOM` would satisfy every bottom-only check, but that function also
  drives layout, rect math and context menus, so lying to all callers mangles
  the taskbar's own geometry. Retained only as a narrowly return-address-filtered
  technique inside approach A.
- **Reimplement auto-hide in the mod.** Highest risk. Would require re-solving
  work-area reservation via `SHAppBarMessage`, fullscreen apps, Start/Search
  being open, flashing taskbar buttons, drag-hover unhide and DPI — all things
  Windows already does correctly. Held in reserve for a single sub-behaviour
  (the unhide trigger) if that turns out to be gated separately.

Keeping Windows in charge of the actual hiding is the main argument for A: work
area release, fullscreen handling and Start-menu interaction come for free.

## Architecture

Two mods, built and validated in sequence, one source file each.

| | Phase 1 | Phase 2 |
|---|---|---|
| id | `taskbar-autohide-any-edge-diag` | `taskbar-autohide-any-edge` |
| behaviour | read-only, changes nothing | the fix |
| output | `%LOCALAPPDATA%\windhawk-autohide-diag.log` | none |

Both `@include explorer.exe` and resolve symbols in `Taskbar.dll` through
`WindhawkUtils::HookSymbols`, guarded on build >= 26200 so they no-op on
unexpected versions instead of misbehaving.

Symbol hooks use the mangled names taken verbatim from this machine's cached
PDB (`Engine/Symbols/Taskbar.pdb/EB0D05FD...`), which matches the live
`Taskbar.dll`. The precedent that `SYMBOL_HOOK` accepts mangled names is the
existing `taskbar-auto-hide-speed` mod. Adjustor thunks (`W7EAA`, `WBI@EAA`
variants) are deliberately not hooked.

### Logging transport

Windhawk's `LoggingVerbosity` is `0` and mods have no on-disk log, so `Wh_Log`
would require DebugView and manual copy-paste. Instead the diagnostic mod writes
its own line-buffered log file to `%LOCALAPPDATA%`, which is readable directly
from WSL at `/mnt/c/Users/<user>/AppData/Local/`. Writes are flushed per line so a
mid-session Explorer restart cannot lose the trace.

## Phase 1 — diagnostic

Hooks, all marked optional so a rename degrades rather than disabling the mod.
Each logs entry arguments, return value, thread id, timestamp, and the live
`GetStuckPlace()` at time of call.

State and flags:

    ?GetAutoHideFlags@TrayUI@@UEAAIXZ
    ?GetDesktopModeAutoHide@TrayUI@@UEBAIXZ
    ?SetAutoHideFlags@TrayUI@@UEAAXI@Z
    ?SetAutoHideState@TrayUI@@UEAAJH@Z
    ?AutoHideChangedOnMonitor@TrayUI@@QEAAXPEAUHMONITOR__@@@Z

Geometry:

    ?_ComputeHiddenRect@TrayUI@@QEAAXPEAUtagRECT@@I@Z
    ?GetDockedRect@TrayUI@@UEAAIPEAUtagRECT@@H@Z
    ?ShrinkRectToStuckEdge@TrayUI@@AEBAXAEAUtagRECT@@H@Z
    ?GetStuckInfo@TrayUI@@UEAAXPEAUtagRECT@@PEAI@Z

Motion and trigger:

    ?SlideWindow@TrayUI@@UEAAXPEAUHWND__@@PEBUtagRECT@@PEAUHMONITOR__@@_N3@Z
    ?Unhide@TrayUI@@UEAAXW4TrayUnhideFlags@TrayCommon@@W4UnhideRequest@3@@Z
    ?SetUnhideTimer@TrayUI@@UEAAXJJ@Z
    ?_AutoHideCollision@TrayUI@@QEAAXXZ

Reference (edge, expected unchanged):

    ?GetStuckPlace@TrayUI@@UEAAIXZ

### Scenario to run

With the taskbar docked left and the diagnostic mod enabled:

1. Toggle "Automatically hide the taskbar" on.
2. Wait ~10 s with the cursor away from the left edge.
3. Sweep the cursor into the left edge and out again, twice.
4. Click the taskbar, then open and close Start.
5. Toggle the setting off.

### Success criterion

The log identifies the last function in the chain that fires. That single fact
determines which branch of phase 2 applies. Phase 1 is complete when we can name
the gate, not when the mod runs.

## Phase 2 — fix, by case

1. **A flag getter returns 0 off-bottom** — hook it to return the enabled value.
2. **An early-out compares the stuck place** — spoof `GetStuckPlace` filtered by
   return address so only that caller is affected.
3. **Hide runs but the rect is wrong** — correct `_ComputeHiddenRect` per edge.
4. **Hide works but unhide-on-hover does not** — supply the trigger in the mod
   (edge hover detection calling `TrayUI::Unhide`). Subject to the input-hook
   constraint below: this must be implemented with in-process cursor polling or
   an existing appbar mechanism, never a global low-level input hook.
5. **Nothing in the chain fires at all** — the gate is upstream of `TrayUI`,
   meaning the auto-hide request never reaches the tray code. Most likely the
   XAML layer or the settings read path. Investigate `Taskbar.View.dll`, whose
   PDB is also cached locally, and the code that reads the auto-hide preference
   alongside `TaskbarLocation`. This is the one case that may require a second
   diagnostic pass rather than a direct fix.

Settings are deliberately minimal: which edges to apply to, and nothing more
until asked. No option is added speculatively.

## Anti-cheat and games

The user plays games with kernel-level anti-cheat, so the mod must not increase
the chance of an anti-cheat incident.

### Verified facts

- Windhawk installs **no kernel driver**. The `Windhawk` service is
  `Type=0x10` (SERVICE_WIN32_OWN_PROCESS, user-mode, LocalSystem) running
  `windhawk.exe -service`. `AppInit_DLLs` is empty.
- Windhawk's engine is nevertheless **injected into all processes by default**,
  minus predefined exclusion lists. Per the project wiki: "the Windhawk engine
  is injected into all processes, except for predefined lists of excluded
  processes, which are system-critical or known to be incompatible."
- Therefore a mod's `@include explorer.exe` limits where the **mod** runs, not
  where the **engine** is injected. Per-mod targeting is not the protection and
  must not be presented as such.
- Installed game libraries: `<GameDrive>:\SteamLibrary`, `<GameDrive>:\SteamLibrary`,
  `<GameDrive>:\SteamLibrary`, `<GameDrive>:\Origin`, `<GameDrive>:\BNet`. The Steam paths match the
  default `steamapps\common\` exclusion. `<GameDrive>:\BNet` is a custom folder name
  unlikely to match the default Battle.net/Blizzard patterns.

### Configuration requirement

Every mod on this machine targets only `explorer.exe`, `<other-app>.exe` and
`StartMenuExperienceHost.exe`, and the new mod adds no fourth target. Windhawk
is therefore to be reconfigured as an explicit allowlist:

- Process exclusion list: `*`
- Process inclusion list: `explorer.exe`, `<other-app>.exe`,
  `StartMenuExperienceHost.exe`

This removes the engine from every game process regardless of install path. The
wiki describes blanket exclusion as a last resort because delayed injection can
break some mods, so this configuration is to be applied and verified **before**
the diagnostic mod is enabled, not after, so that any mod breakage is attributed
correctly.

### Design constraint

The mod must confine its footprint to `explorer.exe`:

- No global or low-level input hooks (`WH_MOUSE_LL`, `WH_KEYBOARD_LL`) and no
  system-wide `SetWindowsHookEx`. Beyond being unnecessary, system-wide input
  interception is exactly the behaviour anti-cheat heuristics look for.
- No injection, memory access or handle acquisition against any process other
  than `explorer.exe`.
- Cursor-position work, if needed, uses in-process polling via `GetCursorPos`
  on an existing tray timer.

### Residual risk

No configuration makes injection-based modding provably safe against kernel
anti-cheat, and this spec does not claim otherwise. The allowlist removes the
known injection vector; it cannot guarantee how a given anti-cheat vendor
classifies a LocalSystem service that performs injection at all. The only
posture with no residual risk is Windhawk not running during a session, via
`windhawk.exe -exit`. Auto-hide simply reverts to stock behaviour while it is
closed, so this costs nothing but the taskbar tweak.

## Error handling and safety

- Explorer is the blast radius. Hooking is all-or-nothing: if any required
  symbol fails to resolve, the mod disables itself rather than running
  half-hooked.
- Phase 1 cannot alter behaviour by construction — every hook calls the original
  and returns its result unmodified.
- Every version is compiled ahead of time in WSL using Windhawk's own
  `clang++.exe` with its `compile_flags.txt`, so code that fails to build is
  never pasted into the editor.
- Escape hatch if Explorer wedges: `windhawk.exe -safe-mode` starts Windhawk
  with all mods disabled. This is documented before the first mod is enabled,
  not after.
- `Wh_ModBeforeUninit` removes hooks and closes the log file cleanly.

## Testing

For each of left, top and right:

- Taskbar hides after the idle delay.
- Cursor at the edge reveals it; leaving re-hides it.
- Stays visible while the taskbar has focus or Start is open.
- Work-area reservation is released when auto-hide engages, so the 48 px is
  returned to applications.
- A fullscreen application does not force the taskbar to appear over it.
- A flashing taskbar button behaves sanely.
- With the allowlist configuration applied, all existing mods still work.
- Disabling the mod restores stock behaviour. An Explorer restart is acceptable
  to fully clear state, but disabling must never leave the taskbar in a stuck
  hidden state that requires one.

## Known limitations

- The second display is out of scope while `MMTaskbarEnabled` is `0`. Secondary
  taskbars use a separate `CSecondaryTray` class with its own `_AutoHide`,
  `_ComputeHiddenRect` and `Unhide`; supporting them is deliberately deferred.
- Symbol hooks are tied to `Taskbar.dll` internals and may break on future
  Windows updates. The version guard limits the damage to "mod stops working"
  rather than "Explorer misbehaves".
