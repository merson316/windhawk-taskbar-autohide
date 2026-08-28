# Roadmap

The left edge works end to end: hides on idle, reveals on hover, stays up while
in use, re-hides on leave, work area released throughout. What follows is the
work still outstanding, in the order it makes sense to tackle it.

## 1. Top and right edges

Implemented and settings-gated, but only the left edge has been exercised. Each
needs the full cycle tested: hide, reveal, stay-up, re-hide, work-area release.

The reveal trigger and stay-visible test are already written generically against
`ABE_*`, so failures are most likely in geometry rather than logic. The hidden
rect for the left edge is `(-46,0)-(2,1440)` on a the primary display display -- a 2px
sliver. Confirm the equivalent on the other edges rather than assuming symmetry.

## 2. Show and hide animation - exhausted at the Win32 layer

The window animates correctly off-bottom: full duration, smooth interpolation,
no interference. It is invisible because the taskbar's pixels are drawn by a
Composition island whose visual position the compositor owns, so moving the HWND
never moves what is on screen.

Eleven hypotheses were tested and eliminated, including three of my own
confident explanations. The table is in `docs/findings-phase1.md`. The decisive
test was animating from a separate thread: 81 window moves over 600 ms with the
UI thread free, still invisible.

Reopening means driving the composition visual directly, alongside
`SharedAnimations::TaskbarCollapseAnimation`. That is a different project, not a
continuation of this one. `ViewCoordinator` is ruled out as an entry point.

## 2b. Known issue: reveal appears to come from the neighbouring display

With a display beyond the docked edge, the hidden position is mostly on that
display -- right-docked on a 3440-wide primary hides to `3438-3486`, and
everything past 3440 is the next monitor. The reveal therefore animates *from*
the neighbour, which reads as the taskbar flying in from the wrong screen.

Mitigation: set the reveal duration to 25 ms so the movement is imperceptible.

A proper fix means the taskbar not occupying the neighbouring display while
hidden, which is the same problem as the hide spilling over there. An attempt at
this is preserved in `research/broken-multimonitor-attempt.wh.cpp`; it ended in a
loop that flickered the taskbar between three positions and was reverted. The
one reusable discovery from it: `SetWindowRgn` clips the taskbar's XAML child
content, where resizing the window does not.

A second attempt declined the edge instead: leave the taskbar visible on any
edge with a display beyond it. That failed for a more fundamental reason.
"Auto-hide is on, but not on this edge" is a state Windows has no concept of, so
every subsystem keyed off auto-hide needs telling separately -- the hooks, the
hidden state, the reserved work area, the Settings toggle, and the transition
between edges. Five fixes produced five new failures and no sign of the last
one. Reverted.

**Resolution: not a defect to fix.** An edge with a monitor beyond it has
nowhere to hide into, so the per-edge setting is the answer -- turn that edge
off and the mod stands down entirely. Anything here should be read as
background, not a plan.

## 3. Secondary monitor support

Nothing here is exercised yet: `MMTaskbarEnabled` is `0` on the development
machine, so no secondary taskbars exist. **The first step is not code** -- enable
"show my taskbar on all displays", dock to a non-bottom edge, and find out
whether the secondary taskbar auto-hides natively. It may not have the bug at
all.

### What transfers from the primary-taskbar work

- Restoring the requested position in `WM_WINDOWPOSCHANGING`. This was the
  central discovery and is not tied to `TrayUI`.
- Driving the slide from the watch thread rather than through `SlideWindow`.
- Pointer polling, the reveal dwell, and the spill handling -- the last is more
  relevant here, since a secondary monitor usually has neighbours on both sides.

### What does not

- `CSecondaryTray` is a different class with different signatures; its
  `GetStuckPlace` takes an out-parameter rather than returning the edge. The
  member offsets this mod depends on -- `0x3c`/`0x44` for flags, `0x110`/`0x118`
  for the edge -- are `TrayUI` layout and must be re-derived.
- The fix hinges on `TrayUI::GetAutoHideFlags` masking the flags off-bottom.
  Whether `CSecondaryTray` does anything equivalent is unknown.
- State is single-taskbar throughout: one `g_trayUI`, one `g_trayWnd`, one
  animation. Multi-taskbar support means per-taskbar state everywhere.

