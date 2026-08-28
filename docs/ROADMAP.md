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

