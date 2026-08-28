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
- Drives reveal and hide from a timer, because Windows' own cursor-at-edge
  detection does not run off-bottom.
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
| Skip edges facing another display | on | Leaves an edge alone when a monitor sits beyond it, since there is nowhere off-screen to hide into. The bottom edge is never touched, as Windows already handles it. |
| Reveal trigger width | 8 px | How close to the screen edge the pointer must get to summon the taskbar. |
| Notifications - keep visible for | 1500 ms | How long a notification keeps the taskbar up before it hides again. **0 turns notification handling off.** |
| Notifications - treat as one within | 5000 ms | Requests closer together than this count as one notification. Only used when the above is not 0. |
| Hide / reveal animation duration | 0 | Overrides the slide duration (25-600 ms). 0 keeps the Windows default. |

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

### A note on the animation duration

The slide is a synchronous loop on the taskbar's own thread, so the duration is
also how long the shell is blocked. Values above a few hundred milliseconds
freeze the taskbar rather than slowing the animation, which is why the range is
capped at 600 ms. A low value such as 25 ms makes hiding effectively instant.

## Known limitations

- **No smooth animation.** The Windows 11 easing is a XAML compositor animation
  that is never triggered off-bottom, so the taskbar moves without it.
- **Primary taskbar only.** Secondary taskbars use a separate `CSecondaryTray`
  class and are not handled.
- **Tied to `Taskbar.dll` internals.** The two member offsets are validated
  against a byte signature inside `GetAutoHideFlags`; if a Windows update
  changes that function, the mod goes inert rather than misbehaving.
- Top and right edges are implemented but have not been tested as thoroughly as
  the left.
- **An edge with another display beyond it cannot hide.** The hidden position
  is real screen space on the neighbouring monitor, so the taskbar would slide
  into view there instead of out of sight. Detected automatically and skipped:
  that edge then behaves as it does with the mod uninstalled. Controlled by
  "Skip edges facing another display". `docs/ROADMAP.md` records why two fixes
  were attempted and abandoned.

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
