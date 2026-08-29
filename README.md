# Taskbar auto-hide on any edge

A [Windhawk](https://windhawk.net/) mod that makes **"Automatically hide the
taskbar"** work when the Windows 11 taskbar is docked to the top, left or right.

Windows 11 26H2 added the ability to dock the taskbar to any edge, but auto-hide
still only functions at the bottom. Elsewhere the setting is accepted, reports
success, and does nothing at all.

## The cause

`TrayUI::GetAutoHideFlags` in `Taskbar.dll` masks the auto-hide flags to zero
unless the taskbar is docked to the bottom:

```asm
cmpl $0x3,0x118(%rbx)     ; edge == ABE_BOTTOM ?
je   .return_real
xor  %eax,%eax            ; otherwise report no flags
ret
.return_real:
mov  0x44(%rbx),%eax      ; the genuinely stored flags
```

The flags are stored correctly on every edge. Everything downstream is gated on
the masked result — `GetDockedRect` returns the hidden rect only when
`(flags & 3) == 3` — so no hide is ever attempted.

## What the mod does

- Reports the genuinely stored flags rather than the masked zero.
- Stores `ABS_ALWAYSONTOP` alongside `ABS_AUTOHIDE` off-bottom, which is what
  the bottom edge does and what the hidden-rect test requires.
- Drives reveal and hide from a watch thread, because Windows' own
  cursor-at-edge detection does not run off-bottom.
- Leaves the bottom edge's auto-hide entirely untouched.
- Hides again a set time after a notification, which Windows otherwise refuses
  to do while a taskbar button is flashing. This applies on every edge and makes
  [Better Taskbar Autohide](https://windhawk.net/mods/taskbar-autohide-better)
  redundant -- run one or the other, since they hook the same function.

## Requirements

Windows 11 build 26200 or newer, x86-64. Developed and tested against
10.0.26300.9278.

## Installation

```
git clone https://github.com/merson316/windhawk-taskbar-autohide.git
```


Windhawk → Create a new mod → replace the contents with
[`src/taskbar-autohide-any-edge.wh.cpp`](src/taskbar-autohide-any-edge.wh.cpp) →
Compile → Enable.

## Settings

| Setting | Default | Meaning |
|---|---|---|
| Hide completely | on | Windows parks a hidden taskbar with a sliver still on screen so the pointer can find it. On the left, top and right edges that reads as a line along the screen edge. The mod tracks the pointer itself, so it hides the taskbar completely. |
| Skip edges facing another display | on | Leaves an edge alone when a monitor sits beyond it, since there is nowhere off-screen to hide into. The bottom edge is never touched, as Windows already handles it. |
| Hide animation duration | 0 | How long the taskbar takes to slide out of view. 0 uses 400 ms; range 25-2000. |
| Reveal animation duration | 0 | 0 keeps the Windows default of 200 ms; range 25-2000. |
| Notifications - keep visible for | 1500 ms | How long a notification keeps the taskbar up before it hides again. **0 turns notification handling off.** |
| Notifications - treat as one within | 5000 ms | Requests closer together than this count as one notification. Only used when the above is not 0. |
| Reveal dwell | 120 ms | How long the pointer must rest in the trigger strip before the taskbar appears. 0 reveals instantly. |
| Reveal trigger width | 8 px | How close to the screen edge the pointer must get to summon the taskbar. |

### Notification handling

When an app wants attention it flashes its taskbar button, and Windows then
refuses to auto-hide the taskbar until the flashing stops. On a busy machine
that can mean the taskbar simply stays on screen.

With `Notifications - keep visible for` set above 0, the taskbar appears once,
stays for that long, then hides again and stays hidden for the rest of the
flashing. Moving the pointer to the edge still reveals it at any time.

This works on **every** edge including the bottom. It replaces [Better Taskbar
Autohide](https://windhawk.net/mods/taskbar-autohide-better); run one or the
other, never both, as they hook the same function.

To disable it and get stock Windows behaviour back, set the value to 0.

### A note on the animation durations

The two durations behave differently, because the two directions are driven by
different code.

The **hide** is animated by the mod on its own watch thread, so its duration
does not block the shell. Windows rewrites intermediate positions in
`WM_WINDOWPOSCHANGING`, forcing each step to the endpoint, which is why an
unmodified vertical taskbar appears to vanish instantly; the mod restores the
requested position after that clamp so the intermediate frames survive.

The **reveal** still goes through Windows' own `SlideWindow`, a synchronous loop
on the taskbar's thread. Its duration is therefore also how long the shell is
blocked, so keep it short -- a few hundred milliseconds at most.

## Nothing happens?

In the order these actually turn out to be the cause:

1. **Auto-hide is not enabled in Windows.** Settings → Personalisation → Taskbar
   → Taskbar behaviours. On a non-bottom edge that toggle can read back as *off*
   even when the registry has it on -- that is the masked `GetAutoHideFlags`
   read this mod exists to fix, so a toggle that turns itself off means the mod
   is not running.
2. **The taskbar is on the bottom edge.** Windows already handles auto-hide
   there and the mod leaves it alone.
3. **The edge faces another display**, so the mod skips it deliberately and that
   edge behaves as it does with the mod uninstalled.

Cases 2 and 3 are the mod working correctly while appearing to do nothing. It
writes which one applies to the Windhawk log once per session, so the answer
does not need a diagnostic build:

```
docked right, Windows auto-hide on -- a display sits beyond this edge, so it is skipped
```

## Known limitations

- **The hide is a linear slide, not the eased Windows curve.** Windows clamps
  every intermediate position to the endpoint, which is why an unmodified
  vertical taskbar appears to vanish instantly; the mod animates the hide itself
  instead. The reveal still uses Windows' own slide. Both durations are
  configurable.
- **Primary taskbar only.** Secondary taskbars use a separate `CSecondaryTray`
  class and are not handled.
- **Tied to `Taskbar.dll` internals.** The two member offsets are validated
  against a byte signature inside `GetAutoHideFlags`; if a Windows update
  changes that function, the mod goes inert rather than misbehaving, and warns
  that it has.
- **An edge with another display beyond it cannot hide.** The hidden position
  is real screen space on the neighbouring monitor, so the taskbar would slide
  into view there instead of out of sight. Detected automatically and skipped:
  that edge then behaves as it does with the mod uninstalled. Controlled by
  "Skip edges facing another display". `docs/ROADMAP.md` records why two fixes
  were attempted and abandoned.

## If it does not work

Every failure mode leaves the taskbar behaving exactly as it does with no mod
installed. That is indistinguishable from the mod working on a machine where
nothing happens to hide, so a silent failure is worse than useless. When the mod
cannot run it stays loaded but completely inert, and the watch thread raises a
dialog naming the reason:

| Warning | Cause |
|---|---|
| Needs build 26200 or newer | Windows is older than the taskbar rework this mod targets. |
| `taskbar.dll` could not be loaded | Should not happen on a supported build. |
| Debug symbols could not be resolved | Windhawk downloads these from Microsoft on first use. Usually no internet, a firewall, or a blocked symbol server. |
| A taskbar function could not be found | Symbols resolved but incompletely. Reloading the mod re-downloads them. |
| `taskbar.dll` does not match | A Windows update recompiled `GetAutoHideFlags`, so the member offsets can no longer be trusted. The mod needs updating. |
| Could not attach to the taskbar window | The taskbar never appeared, or subclassing it failed. |

Failures are deliberately not fatal to loading. Returning `FALSE` from
`Wh_ModInit` makes Windhawk unload the mod immediately, which would pull the DLL
out from under the thread that raises the warning. Staying loaded and inert is
behaviourally identical and keeps that thread alive.

The dialog is raised through `WTSSendMessageW`, so the session owns it and
nothing in the mod waits on the user. `MessageBoxW` parks its calling thread
inside mod code until the button is clicked, and Windhawk holds the unload until
every mod thread has exited: disabling the mod, which is what the warning asks
for, would hang the uninitialize.

Only the process that owns `Shell_TrayWnd` ever warns. Windhawk injects into
every `explorer.exe`, including the separate processes folder windows can use,
and those have no taskbar to manage.

## Anti-cheat note

Windhawk's engine injects into processes broadly by default, so a mod's
`@include` is not what keeps it out of games. If you play games with kernel
anti-cheat, add your game library paths to Windhawk's process exclusion list.
See [`docs/windhawk-config.md`](docs/windhawk-config.md).

## Development notes

[`docs/findings-phase1.md`](docs/findings-phase1.md) records how this was found,
including the wrong turns: three attempts assumed the flag *store* was refused
when it never was, and the pointer-base table there explains both an Explorer
crash and a silent no-op.

`tools/whcompile.sh` compiles and links a mod using Windhawk's own clang, so
build errors surface before pasting into the editor.

Layout: `src/` holds the mod that ships. `dev/` holds the read-only diagnostic
builds and an experiment build with its own `@id`, so they can be installed
alongside the real one. `research/` keeps the traces and the abandoned attempts,
which are worth reading before retrying either.

## License

GPL-3.0, matching the Windhawk mod ecosystem. See [LICENSE](LICENSE).
