# Taskbars on every display, and the four open issues

> **Status, end of 2026-11-15: built, with these departures from the text below.**
> The text is kept as the record of what was decided before the work; where it
> and this note differ, this note and the code are right.
>
> - **A held taskbar does not slide half-way and wipe.** Its window stays on its
>   shown position; its content window is slid inside it and a region keeps the
>   visible band. Half-slide plus wipe, a cut and a proportional move were all
>   tried on a real desktop and looked wrong against the stock bottom edge.
> - **No cloaking.** Windows has one cloak switch for a taskbar and uses it
>   itself; the two undid each other.
> - **On the bottom edge the slide runs inside `TrayUI::SlideWindow`**, before
>   Windows clips the taskbar and hides its content.
> - **Edge changes and secondary unhides go through window messages**
>   (`0x5CA` setting 6 and `0x5B2`; `0x5BE`), read out of `taskbar.dll`, so they
>   need no symbols.
> - **Off the bottom edge every hidden taskbar hides completely**, the
>   secondaries included.
> - **The taskbar stays up while Quick Settings is open on the bottom edge too**
>   (owner's decision), which Windows alone does not do.
> - **Testing is by `tools/test-taskbars.ps1`**, not by hand: it drives the
>   desktop, measures the windows and looks at the screen. Run with the mod off,
>   it reproduces issue #11 in Windows itself (the hidden upper taskbar at the
>   lower display's scaling).
> - Still open: the layouts in `docs/ROADMAP.md` section 4.

Status: approved by the owner on 2026-11-15. Experimental branch
`multimonitor-experiment`. Covers issues #9, #10, #11 and #12.

## Intent

The mod was built for one taskbar on one display. Other people now use it
with "show my taskbar on all displays" and with displays of different sizes,
positions and scaling, and four issues came out of that.

The largest is #11. Windows hides a taskbar by moving its window past the
docked edge. When another display lies past that edge, the hidden window sits
mostly on that display, takes that display's scaling, and is revealed from
that display's edge. On reveal the icons are drawn at the wrong size and then
snap. This is Windows' own behaviour, measured with the mod not loaded (see
"What was measured").

After this change a hidden taskbar stays on its own display, keeps its own
scaling, is revealed from its own display, and every taskbar moves at the
refresh rate of the display it is on. The same change removes the reason for
"Skip edges facing another display": an edge with a display beyond it becomes
an edge the mod can handle.

The three smaller issues are fixed in the same release because they touch the
same code: a flashing button that keeps re-showing taskbars (#9), a taskbar
that hides under an open Quick Settings panel (#10), and a hide delay setting
(#12).

## Goals

1. A hidden taskbar never has more than half its area on another display, on
   any display it belongs to. Its DPI never changes while it hides or reveals.
2. A taskbar on an edge with a display beyond it is revealed by resting the
   pointer in a strip on its own display. The neighbouring display's edge no
   longer reveals it.
3. Hide and reveal run at one step per refresh of the taskbar's own display,
   up to the frame rate setting.
4. A taskbar on an edge with no display beyond it slides fully off the
   desktop, as now. A taskbar on an edge with a display beyond it slides for
   just under half its thickness and the rest is wiped. The owner chose this
   over a plain wipe because the icons rise with the taskbar, as on the
   primary.
5. A button that keeps flashing shows each taskbar once for "keep visible
   for", then leaves it hidden, on every display (#9).
6. A taskbar stays up while a Quick Settings panel is open, whichever window
   has focus (#10).
7. A setting delays the hide after the pointer leaves or a shell surface
   closes (#12). 0 keeps today's behaviour.
8. Everything above works on taskbar.dll 10.0.26100.10066, whose
   `SlideWindow` Microsoft reworked, as well as on the builds before it.

## Non-goals

- Different edges on different displays. Windows docks every taskbar to one
  edge and the mod keeps that.
- A full slide on an edge with a display beyond it. Moving more than half the
  window onto the other display is what changes its DPI.
- Windows' eased curve for the slide. The mod's own easing stays.
- Fixing the content animation Windows runs inside the taskbar on reveal.

## Anti-goals (unacceptable even if the features work)

- Explorer crashing or hanging. In particular, never swallow or alter
  `WM_DPICHANGED`: the reporter of #11 found that crashes Explorer.
- A taskbar left stuck on screen, or stuck hidden with no way to reveal it.
- A change in behaviour for someone with one display and the taskbar at the
  bottom. That case stays Windows' own.
- A wrong address patched on a build without symbols. New hooks follow the
  existing rule: validated before patching, and the mod stands down with a
  message when it cannot be sure.
- Loosening the notification rule so far that a Win-key or keyboard reveal is
  dropped.

## What was measured

All on 26100.9974, display 5 (1440x2560, 125%, 60 Hz) stacked above the
primary (3440x1440, 100%, 240 Hz), taskbar at the bottom, auto-hide on.

With the mod not loaded (`tools/measure-taskbars.ps1`):

| Secondary taskbar | Rect | On its own display | On the primary | DPI |
|---|---|---|---|---|
| Hidden | (988,-2,2428,58) | 2 px | 58 px | 96 |
| Shown | (988,-60,2428,0) | 60 px | 0 px | 120 |

The DPI changes mid-slide, as soon as most of the window is on the primary.
Reveals were logged with the pointer on the primary's top row. The owner saw
nothing drawn on the primary while hidden, and wrong-size icons on reveal.
The same parking was seen once on the left edge.

With the throwaway mod `research/spike-secondary-park.wh.cpp`, versions 0.1
to 0.5:

- Replacing the position Windows asks for, in `WM_WINDOWPOSCHANGING`, holds
  the window on its own display. Windows does not fight it: no position loop
  in any run.
- With the window held, the DPI stayed 120 through every hide and reveal, and
  no `WM_DPICHANGED` arrived, including when the window moved 28 of its 60 px.
- `SetWindowRgn` hides the part of the taskbar that should not show. It also
  limits where the window receives the pointer.
- Sending the held window `WM_NCHITTEST` and `WM_MOUSEMOVE` at a point inside
  its visible sliver makes Windows reveal it, about 65 ms later. Nine of nine
  attempts worked. Which of the two messages matters was not separated.
- The secondary taskbar's slide goes through `TrayUI::SlideWindow`. Calling
  the original with `animate = false` makes Windows ask for the end position
  once and leaves the taskbar's thread free.
- An animation paced by a high-resolution waitable timer on its own thread,
  with each step run on the taskbar's thread, kept to the frame: gaps of 13.2
  to 17.9 ms against 16.7 ms at 60 Hz with none late, and 49 to 61 steps per
  animation at 240 Hz with one late step in sixteen animations.
- The primary's position handler replaces any position it is given, 34 to 41
  times per animation. The mod's own moves must be put back after it runs.
  The secondary's handler does not do this.
- Windows' own reasons to keep a taskbar up still work on a held taskbar: it
  stayed up while the pointer was over a thumbnail preview.

Quick Settings (`tools/measure-taskbars.ps1 -Class ControlCenterWindow`,
seven open-and-close cycles): there is one `ControlCenterWindow`. Before its
first use it is not visible with an empty rectangle. Open, it is visible at
(3056,0,3440,1392) on the primary. Dismissed by Esc, by a click elsewhere or
by Win+A, it keeps that rectangle and stops being visible. It was never
cloaked. So "visible, not cloaked, on a display" tells open from dismissed.

Secondary taskbars and notifications (9974's PDB and code):
`CSecondaryTray::Unhide(UnhideRequest)` is a four-instruction thunk that
adjusts `this` and jumps to the private `_Unhide(TrayUnhideFlags,
UnhideRequest)` with flags 0, so hooking `_Unhide` alone sees every request
once. Sending a held taskbar `WM_NCHITTEST` alone reveals it (five of five,
about 62 ms later); no mouse move is needed. Pressing the Win key with the
pointer on the upper display opens Start on the primary with the mod disabled
too.

Not tested: left, right and top edges with a display beyond them; three or
more displays; edges that only partly face another display; Start, Search and
Quick Settings opened from a held taskbar; a held taskbar together with the
main mod; build 10066.

## How it works (decided)

### 1. One state per taskbar

Today there is one `g_trayUI`, one `g_trayWnd` and one animation. The mod
keeps a record per taskbar window (`Shell_TrayWnd` and every
`Shell_SecondaryTrayWnd`): its display, that display's refresh rate, whether a
display lies beyond its docked edge, its shown position, how much of it is
hidden now, its animation, and its reveal dwell. Records are made when a
taskbar window appears and dropped when it is destroyed, and the display
facts are refreshed when the display layout changes.

### 2. Holding a taskbar on its own display

A taskbar is **held** when a display lies beyond its docked edge. For a held
taskbar the mod:

- replaces every position Windows asks for that reaches past the taskbar's
  own display with the position the mod's animation is at;
- lets the window travel at most `thickness / 2 - 2` pixels past the edge;
- uses a window region to hide what should not show: the overhang during the
  travel, then the rest of the taskbar, down to the sliver Windows wanted
  left.

A taskbar that is not held slides off the desktop as today.

"A display lies beyond the edge" means any display touches a one-pixel band
just past the docked edge, along the taskbar's length. An edge that only
partly faces another display counts as facing it. (Builder's call, flagged
for review: the owner has not ruled on partial overlap.)

### 3. Revealing a held taskbar

Windows reveals a held taskbar only from the sliver left on screen, which is
too thin to hit when the pointer can carry on into the next display. The mod
watches a strip along the docked edge on the taskbar's own display, as deep
as "Reveal trigger width" scaled by that display's DPI. When the pointer has
rested there for the dwell, the mod makes Windows reveal the taskbar.

On a held edge the dwell is the larger of "Reveal dwell" and 150 ms, because
the pointer crosses that strip every time it moves between the displays.
(Builder's call, flagged for review.)

### 4. The mod runs the animation

For every taskbar the mod handles, `TrayUI::SlideWindow` is called with
`animate = false` and the mod animates to the position Windows asked for.
One thread paces all animations from a high-resolution timer; each step runs
on the taskbar's thread. A taskbar steps once per refresh of its own display.

"Animation frame rate" becomes a cap on that rate. It keeps its range of 30
to 240 and its default of 90 (the owner's decision): a 60 Hz display steps at
60, a 240 Hz display at 90, and setting 240 lets a 240 Hz display step at its
own rate. There is no "no cap" value.

Measured on the owner's 240 Hz primary (spike 0.6, CPU time inside Explorer
per animation, from cycle counts; two runs per rate except five at 240):

| | Hide (250 ms) | Reveal (200 ms) |
|---|---|---|
| Windows' own slide | 7 ms | 10 ms |
| Mod at 60 fps | 18 ms | 17 ms |
| Mod at 90 fps | 21 ms | 24 ms |
| Mod at 120 fps | 27 ms | 26 ms |
| Mod at 240 fps | 41 ms | 48 ms |

Nearly all of it is on the taskbar's thread; the pacing thread costs 0.4 to
2.4 ms. The cost follows the number of steps, at roughly 0.7 to 1.1 ms a
step. The compositor's share could not be read (dwm.exe refused the query),
so the whole cost is higher than these numbers by an unknown amount.

This replaces the `Sleep` and `GetTickCount` hooks that pace Windows' own
loop today, and the read of the two duration members inside `SlideWindow`.
The duration settings then apply to the mod's own animation and no longer
depend on where a build keeps those members.

### 5. Where the mod acts

| Layout | Bottom edge | Left, top, right |
|---|---|---|
| No taskbar is held | Windows' own, untouched | As today |
| At least one taskbar is held | The mod animates every taskbar and holds the held ones | Same, once tested (see delivery) |

So that all taskbars on a desk move alike, the mod takes over the bottom edge
whenever any taskbar there is held, and only then. (Builder's call, flagged
for review.)

"Skip edges facing another display" stays as a way out, and its default
becomes off (the owner's decision), since this work is what makes those edges
usable. While it is on, the mod stands down on the left, top and right edges
that face a display, as today. The default changes in the release that brings
held taskbars to those edges, not before: until then, turning it off gives
the old behaviour the setting exists to avoid.

### 6. Flashing buttons (#9)

- A request that differs from the one that began the running burst is let
  through, but does not begin a new burst. Today each stray request causes
  two extra reveals; this leaves one. Swallowing every request inside a
  burst, as the reporter did, risks dropping a Win-key reveal.
- The exception that lets a repeat through while the shell has focus uses
  `ShellSurfaceInForeground()`, not `ShellHasFocus()`, which is true for the
  desktop and for any File Explorer window.
- "Treat as one within" defaults to 10000 ms. Some apps flash every 5.6 s.
- Secondary taskbars: the same burst rule on `CSecondaryTray::_Unhide`. Only
  that one function is hooked, since `Unhide` probably calls it and hooking
  both would count each request twice. The hook is set from symbols only. On
  a build without published symbols it is skipped, with a log line, and the
  rest of the mod works. It does not join `kKnownBuilds`.
- The burst state is kept per taskbar.

### 7. Quick Settings (#10)

- `ShellHost.exe` joins `kShellHosts`.
- A taskbar is also kept up while any `ControlCenterWindow` is visible, not
  cloaked, and on a display. There can be more than one such window.
- No separate grace period. The hide delay below covers it.

### 8. Hide delay (#12)

A new setting, `hideDelayMs`, "Hide delay": how long a taskbar stays up after
the last moment something was keeping it up (the pointer over it, a shell
surface in front, a notification hold). Default 0, range 0 to 10000. The
pointer returning restarts it. It applies wherever the mod decides the hide.
On a bottom edge the mod has left to Windows it has no effect, and the
setting's description says so.

### 9. Order of delivery

1. `slidewindow-10066` lands first, on its own: the duration scan, the
   matcher's vtable-slot evidence, and through the watcher the 10066 build.
2. The small fixes that do not need per-taskbar state: #9 except the
   secondary hook, #10, #12.
3. Per-taskbar state, the hold, the reveal strip and the mod's own animation,
   on the bottom edge.
4. Held taskbars on the left, top and right edges, and "Skip edges facing
   another display" off by default.

The owner has since decided that all four steps ship as one release, tested
on every edge before it goes out, and that the release is **v2.0.0**. The steps remain the order of work, and
each still leaves the mod working. A release with code changes uses the manual
`tools/release.sh` flow and takes the next minor version; an open build PR is
closed and reopened by the watcher, as the README describes.

## Testing

**Pure logic, in CI.** The decisions move into functions that take plain
inputs and are compiled with Windhawk's compiler in the Windows job, as
`tests/test_mod_guard.py` does now:

| Function | Cases |
|---|---|
| Which taskbars are held, from display rectangles and the docked edge | Side by side, stacked, L-shape, offset, different sizes, negative coordinates, three displays, partial overlap |
| Window position and region for N hidden pixels | Each edge; held and not held; the window never has more than half its area off its display |
| Reveal strip | Each edge at 100, 125, 150 and 200% |
| Burst rule | Stray request mid-burst; 5.6 s flashes; two taskbars interleaved; focus states |
| Quick Settings hold | Open, dismissed, cloaked, two panels |
| Hide delay | 0; pointer returns mid-delay; setting changed while waiting |

**Layout matrix, on the owner's PC.** A script sets a display layout, drives
the pointer and a flashing button, and asserts on what
`tools/measure-taskbars.ps1` reads: each taskbar's rectangle, its DPI, and
the pixels it has on each display. Pass means the DPI never changes and no
hidden taskbar is mostly on another display. The owner's two displays cover
stacked and side by side at several scalings.

**By eye, each release.** Smoothness of the half slide, repaint, and icon
size on reveal. The script cannot see these.

**The reporter.** They have the layout from #11 and a working fork. A
pre-release build goes to them before the release.

Not covered: three or more real displays. A virtual display driver may
provide them; whether the taskbar treats one as a display is unknown.

## Left to the builder

- How records are found from hooks that receive a `TrayUI` or
  `CSecondaryTray` pointer and not a window.
- Whether auto-hide works at all for a secondary taskbar on the left, top or
  right edge. The roadmap notes this was never tried. Measure before coding.
- `CSecondaryTray`'s layout. The member offsets the mod uses are `TrayUI`'s
  and are not valid on a `CSecondaryTray`.
- Which of `WM_NCHITTEST` and `WM_MOUSEMOVE` triggers the reveal, and whether
  calling the unhide function directly is better where symbols allow.
- How a dismissed `ControlCenterWindow` looks (cloaked, hidden or moved). If
  the test is wrong the taskbar never hides, so confirm on Windows first.
- Start, Search and Quick Settings opened from a held taskbar: where they
  anchor, and whether the region clips them.
- Putting everything back when the mod unloads or a taskbar stops being held:
  region removed, window where Windows last asked.
- The README settings table, the header settings block, and
  `docs/ROADMAP.md` sections 2b and 3, which this supersedes.
