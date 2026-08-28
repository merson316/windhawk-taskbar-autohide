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
- Leaves the bottom edge entirely untouched.

## Requirements

Windows 11 build 26200 or newer, x86-64. Developed and tested against
10.0.26300.9278.

## Installation

Windhawk → Create a new mod → replace the contents with
[`src/taskbar-autohide-any-edge.wh.cpp`](src/taskbar-autohide-any-edge.wh.cpp) →
Compile → Enable.

## Settings

| Setting | Default | Meaning |
|---|---|---|
| Enable on the left / top / right edge | on | Which edges the mod acts on |
| Reveal trigger width | 4 px | How close to the edge the pointer must get |

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

## License

GPL-3.0, matching the Windhawk mod ecosystem. See [LICENSE](LICENSE).
