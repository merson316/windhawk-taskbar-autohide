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
- Handles every taskbar, on every display, at each display's own scaling and
  refresh rate. See [More than one display](#more-than-one-display).
- Animates the taskbar itself, on every edge it handles, so the slide is the
  same everywhere and runs at the display's frame rate.
- Keeps the taskbar above other windows when it reveals. Off the bottom edge
  a revealed taskbar can otherwise come up behind whatever covers that edge.
- Hides again a set time after a notification, which Windows otherwise refuses
  to do while a taskbar button is flashing. This applies on every edge and makes
  [Better Taskbar Autohide](https://windhawk.net/mods/taskbar-autohide-better)
  redundant -- run one or the other, since they hook the same function.
- Keeps the taskbar up while Quick Settings is open, on every edge. On the
  bottom edge Windows alone hides it under the open panel.
- Brings the same to the bottom edge: no line of taskbar left on screen, the
  hide delay, the reveal dwell and trigger width. Windows still decides there
  when a taskbar hides; the mod carries it out.

## Requirements

Windows 11 build 26200 or newer, x86-64. Developed and tested against
10.0.26300.9278.

The mod finds the taskbar's functions through Microsoft's debug symbols, which
Windhawk downloads on first use. Microsoft sometimes publishes them days after a
Windows update. For builds where that has happened, the mod carries the function
addresses itself, matched by hand against the previous build's symbols, and
uses them only on that exact `taskbar.dll` and only when the symbols cannot be
resolved:

| `taskbar.dll` | Matched against |
|---|---|
| 10.0.26100.9807 | 10.0.26100.9549 |
| 10.0.26100.9974 | 10.0.26100.9818 |
| 10.0.26100.10066 | 10.0.26100.9974 |

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
| Hide completely | on | Windows parks a hidden taskbar with a sliver still on screen so the pointer can find it. On the left, top and right edges that reads as a line along the screen edge. The mod tracks the pointer itself, so it hides every taskbar completely, on every edge. Turn this off to keep Windows' sliver; on the bottom edge, with no display below, that also hands the taskbar back to Windows' own slide. |
| Leave edges facing another display alone | off | With a display beyond the taskbar's edge there is nowhere to hide into, so the mod keeps the taskbar's window where it is and slides its content out of it. Turn this on to leave such an edge alone instead. The bottom edge is not affected. |
| Animation frame rate | 90 | The most frames per second for both slides; each taskbar steps at its own display's refresh rate up to this. Higher is smoother and costs more CPU; range 30-240. Disable "Taskbar auto-hide speed" if you have it — both mods pace the same reveal. |
| Hide animation duration | 0 | How long the taskbar takes to slide out of view. 0 uses 400 ms; range 25-2000. |
| Reveal animation duration | 0 | 0 keeps the Windows default of 200 ms; range 25-2000. |
| Hide delay | 0 ms | How long the taskbar stays up after the pointer leaves it, or after Start or a flyout closes, before it hides. The pointer coming back starts the wait again. Applies on every edge. Range 0-10000. |
| Notifications - keep visible for | 1500 ms | How long a notification keeps the taskbar up before it hides again. **0 turns notification handling off.** |
| Notifications - treat as one within | 10000 ms | Requests closer together than this count as one notification. Only used when the above is not 0. |
| Reveal dwell | 0 (off) | How long the pointer must rest in the trigger strip before the taskbar appears. Off by default; raise it if the taskbar appears when you were only crossing the edge. A pointer already over the taskbar never waits. |
| Reveal trigger width | 8 px | How close to the screen edge the pointer must get to summon the taskbar. |
| Keep the taskbar above other windows | on | Off the bottom edge a revealed taskbar can come up behind whatever sits over that edge. This puts it back in front as it appears. Left alone when a full-screen window is in front, where Windows lowers the taskbar deliberately. |

### Notification handling

When an app wants attention it flashes its taskbar button, and Windows then
refuses to auto-hide the taskbar until the flashing stops. On a busy machine
that can mean the taskbar simply stays on screen.

With `Notifications - keep visible for` set above 0, each taskbar appears once,
stays for that long, then hides again and stays hidden for the rest of the
flashing. Moving the pointer to the edge still reveals it at any time.

This works on **every** edge including the bottom. It replaces [Better Taskbar
Autohide](https://windhawk.net/mods/taskbar-autohide-better); run one or the
other, never both, as they hook the same function.

To disable it and get stock Windows behaviour back, set the value to 0.

### Keeping the taskbar on top

Off the bottom edge a revealed taskbar can come up underneath a window covering
that edge. The taskbar keeps `WS_EX_TOPMOST` throughout; what it loses is its
place in the z-order. What causes this is outside `taskbar.dll`: there the
taskbar is only sent to the bottom for a full-screen window, and that clears
the topmost style.

The mod re-asserts `HWND_TOPMOST` before the reveal slide starts, while the
taskbar is still off-screen, so the correction is not visible.

It is skipped while a full-screen window is in front. Windows lowers the
taskbar there deliberately, and overriding that would put the taskbar over
full-screen games and video. Turn off "Keep the taskbar above other windows" to
leave z-order entirely to Windows.

### More than one display

With "Show my taskbar on all displays" on, each display has a taskbar, and the
mod handles each one by itself: its own display, scaling and refresh rate.

A taskbar whose edge has another display beyond it has nowhere to hide into.
Windows slides it onto the neighbouring display and clips it there; the hidden
window then takes that display's scaling, which is why its icons can come back
the wrong size when the two displays are scaled differently. The mod never
moves such a taskbar's window. It slides the taskbar's content out of the
window and keeps only the visible band, so nothing is ever drawn on the other
display and the scaling never changes. This applies on every edge, the bottom
included.

Such a taskbar is revealed by resting the pointer on the edge of its own
display for a moment (at least 150 ms, in a strip at least 12 px deep), since
the pointer also crosses that edge on its way to the next display.

Off the bottom edge the mod hides and reveals the secondary taskbars itself, as
it does the primary. On the bottom edge Windows decides when each taskbar hides
and the mod carries the movement out.

On the bottom edge, with "Hide completely" on, every taskbar is handled the way
a held one is, whether or not a display lies below it: the window stays, the
content slides, nothing is left on screen, and the pointer resting on the
display's edge brings it back. That is what gives the bottom edge the reveal
dwell and trigger width. The hide delay there is a refusal: Windows asks the
taskbar whether it may hide, and the mod says no until the delay has passed.

### How the animation works

Windows rewrites every requested taskbar position in `WM_WINDOWPOSCHANGING`,
forcing each step to the endpoint, which is why an unmodified vertical taskbar
appears to vanish instantly. The mod moves the taskbar itself and puts the
requested position back after that rewrite, so the frames in between survive.

Each taskbar steps on a clock of its own: `QueryPerformanceCounter` for the
time and a high-resolution waitable timer for the wait, once per refresh of its
display up to the frame rate setting, easing into the end position. Two
taskbars on a 60 Hz and a 240 Hz display each keep their own pace.

On the bottom edge Windows asks for a hide or a reveal through
`TrayUI::SlideWindow` and, right after a hide, clips the taskbar and makes its
content invisible. There the mod runs its slide inside that call, which is
where Windows' own blocking slide loop would have run, so the taskbar is still
drawn while it moves.

When the mod leaves a bottom-edge taskbar to Windows ("Hide completely" off
and no display below it), the slide is Windows' own loop, and the mod only
paces it:
`Sleep` and `GetTickCount` are hooked and take effect only on the thread
currently inside `SlideWindow`, replacing the inter-frame sleep with a frame
budget and reporting a clock fine enough to separate the frames it buys. This
is the technique
[Taskbar auto-hide speed](https://github.com/ramensoftware/windhawk-mods) uses;
run one mod or the other, not both, or they will fight over the same loop.

## Nothing happens?

In the order these actually turn out to be the cause:

1. **Auto-hide is not enabled in Windows.** Settings → Personalisation → Taskbar
   → Taskbar behaviours. Windows greys that option out when the taskbar is not
   at the bottom, so set it with the taskbar at the bottom and then move it.
   The mod raises a dialog when auto-hide has stayed off for two minutes.
   (Windows reports it off for about a minute after Explorer starts with the
   taskbar off the bottom edge; that settles by itself.)
2. **The taskbar is on the bottom edge, with "Hide completely" off and no
   display below it.** Windows already handles auto-hide there and the mod
   leaves it to Windows.
3. **"Leave edges facing another display alone" is on** and the edge faces
   another display, so that edge behaves as it does with the mod uninstalled.

Cases 2 and 3 are the mod working correctly while appearing to do nothing. It
writes which one applies to the Windhawk log once per session, with a line for
each taskbar it found:

```
taskbar 00000000000F0600 (primary) at (0,0,48,1440): display (0,0,3440,1440) at 240 Hz, edge 0, not held
docked left, Windows auto-hide on -- active
```

## Known limitations

- **Tied to `Taskbar.dll` internals.** The two member offsets are read out of
  `GetAutoHideFlags` at load, by finding the instruction sequence that encodes
  them rather than requiring it at a fixed offset. A recompiled prologue or a
  moved struct member is therefore survivable; a rewritten function is not, and
  the mod goes inert and warns rather than trusting offsets it cannot confirm.
  The mod also relies on three window messages of the taskbar (to follow a
  change of edge and to unhide a secondary taskbar) and on the class of the
  window that draws it. `tools/test-taskbars.ps1` checks those on a running
  taskbar.
- **On the bottom edge Windows decides when a taskbar hides.** The mod can
  refuse a hide (for the hide delay, and while Quick Settings is open) but
  does not start one.
- **Without symbols for a build, one function may be missing.** A flashing
  button is kept from re-showing a secondary taskbar through
  `CSecondaryTray::_Unhide`. The mod carries its address for the builds in the
  table above where it could be matched with certainty; where it could not,
  a secondary taskbar on the bottom edge comes up again for each flash.
- **Tested layouts.** Two displays stacked, at the same and at different
  scaling, on all four edges. Displays side by side, three or more displays,
  and displays that only partly share an edge follow the same rules but have
  not all been run.

## If it does not work

Every failure mode leaves the taskbar behaving exactly as it does with no mod
installed. That is indistinguishable from the mod working on a machine where
nothing happens to hide, so a silent failure is worse than useless. When the mod
cannot run it stays loaded but completely inert, and the watch thread raises a
dialog naming the reason. It also raises one when it is running but Windows'
auto-hide setting is off:

| Warning | Cause |
|---|---|
| Needs build 26200 or newer | Windows is older than the taskbar rework this mod targets. |
| `taskbar.dll` could not be loaded | Should not happen on a supported build. |
| Microsoft has not published the debug symbols for this version yet | A Windows update replaced `taskbar.dll`, and Microsoft's symbol server does not have its PDB yet. The mod tries again each time Windows starts and recovers by itself once the PDB is published. |
| Microsoft has published the debug symbols, but Windhawk could not load them | The symbol server has the PDB, but the download or load failed. Reloading the mod downloads it again. |
| Debug symbols could not be resolved | The symbol server could not be reached to say which of the above applies. Usually no internet, a firewall, or a blocked symbol server. |
| The built-in addresses did not check out | No symbols are published, the mod carries addresses for this exact build, but one of them is not where a function starts. The mod leaves the taskbar alone rather than patch the wrong code. Please report it. |
| A taskbar function could not be found | Symbols resolved but incompletely. Reloading the mod re-downloads them. |
| `taskbar.dll` does not match | A Windows update recompiled `GetAutoHideFlags`, so the member offsets can no longer be trusted. The mod needs updating. |
| Could not attach to the taskbar window | The taskbar never appeared, or subclassing it failed. |
| Auto-hide is turned off in Windows | The mod is running correctly and has nothing to do. The most common reason the mod appears not to work. |

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
anti-cheat, add your game library roots to Windhawk's process exclusion list
(Settings → Advanced settings → More advanced settings → Process exclusion
list), one per line, for example `D:\SteamLibrary\*` and `C:\XboxGames\*`. The
list adds to Windhawk's built-in exclusions, and games installed under a listed
root are covered automatically.

## Development notes

[`docs/findings-phase1.md`](docs/findings-phase1.md) records how this was found,
including the wrong turns: three attempts assumed the flag *store* was refused
when it never was, and the pointer-base table there explains both an Explorer
crash and a silent no-op.

`tools/compile-mod.sh` compiles the mod with Windhawk's own compiler and the
exact arguments Windhawk uses, so build errors surface before pasting into the
editor. CI runs the same script on every push and pull request.

`tools/test-taskbars.ps1` drives the taskbars on a real desktop and checks what
they do, in place of testing by hand. It moves the pointer, presses Win and
Win+A, flashes a button and, with `-Edges 3,0,1,2`, moves the taskbar to each
edge in turn, then puts the pointer and the edge back. At about 1 ms intervals
it measures each taskbar's window (how much is in view on its own display, how
much on another, its scaling, where its content is) and looks at the strip of
screen where the taskbar shows, so a slide that jumps, steps back, shows on
the neighbouring display or is not drawn fails. Run it with the mod disabled
to see what Windows does by itself. `-Snapshot` only looks. Moving the mouse
stops a run.

The shipped mod logs what it found and what stopped it, a line each. A debug
build also logs every request Windows makes of a taskbar and every slide. It is
the same source with one line changed (`tools/make_debug_build.py`), and each
release carries it as `taskbar-autohide-any-edge.debug.wh.cpp`, for reporting a
problem.

`tools/install-dev-build.py` installs the working copy in Windhawk through the
same elevated task a release uses, as the installed version with its last
number raised by one.

`.github/rulesets/` holds the rules for `main`. `main: history` blocks
force-pushes and deletion and requires signed commits, with no exceptions.
`main: CI` requires both CI jobs to pass, but admins can bypass it, because
`tools/release.sh` pushes its compatibility-record commits straight to `main`.
Release tags (`v*`) can't be moved or deleted (`tags: history`, no
exceptions), and only admins can create them (`tags: release`): the watcher
trusts a `v*` tag on `main` as its own unfinished release. Outside the
rulesets, Actions are limited to GitHub's own, pinned by commit SHA, and
secret scanning with push protection is on.
GitHub only offers rulesets on public repos (or with Pro); to apply them:

    for f in .github/rulesets/*.json; do gh api -X POST repos/{owner}/{repo}/rulesets --input "$f"; done

Layout: `src/` holds the mod that ships. `tools/` holds the build check, the
matcher for taskbar builds without published symbols, the build watcher (at
sign-in, and when an update has staged a new build and is waiting to restart) and
the release script; `tests/` covers them (`.venv/bin/pytest tests`). The
diagnostic builds, raw traces and abandoned attempts from the original
investigation were removed from the tree after v1.17.0; `git show
v1.17.0:<path>` brings any of them back, and they are worth reading before
retrying either.

New taskbar builds need no hands. When a build has no published symbols, the
watcher matches it and opens a ready PR. Once both CI checks pass on that exact
commit, it releases the PR (signed rebase, signed tag, GitHub release marked
"Not tested by hand."), closes the other ready build PRs (drafts are left alone), and installs the release in
the owner's Windhawk. A toast says `v... released and installed for taskbar ...`.
Windhawk reloads the mod when its registry `LibraryFileName` changes, so no
restart is needed. A ready build PR that main has moved past for another reason (a manual
release, say) is closed the same way, and the watcher reopens it from the new main.
To stop a release, convert the PR to a draft. Closing it also stops it, but
only if you keep its branch: with the branch deleted, the next run matches the
build again and opens a new ready PR.
The watcher only releases a same-repo `build/*` PR signed with this PC's key
that adds one `kKnownBuilds` entry, bumps the version and adds one README row
(`tools/check_build_pr.py`).

The matcher answers for the functions the mod hooks: seven it must find, and
`CSecondaryTray::_Unhide`, which it adds when it can match it with the same
certainty and leaves out otherwise. What it cannot answer for is checked on a
running taskbar by `tools/test-taskbars.ps1`; the new-build PR lists that run.

The install runs as an elevated scheduled task, `WindhawkTaskbarModInstall`,
which runs `tools/install-mod.ps1` from the staging folder
(`%LOCALAPPDATA%\taskbar-autohide\install`), where the watcher copies it each
time. That folder is one the owner's account can edit, so the task guards against mistakes, not against malware that
already runs as the owner. The DLL imports Windhawk's `libc++.whl` and `libunwind.whl`
from `Engine\Mods\64`; when Windhawk's compiler ships different ones, the task puts them in
place first and renames the old ones to `<name>_tempN.whl`, as Windhawk's own compile does. Register both tasks once from an elevated
PowerShell with `tools/install-watch-task.ps1` (`-Uninstall` removes both).

## License

GPL-3.0, matching the Windhawk mod ecosystem. See [LICENSE](LICENSE).
