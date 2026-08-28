# Roadmap

Left and top work end to end: hide on idle, reveal on hover, stay up while in
use, re-hide on leave, work area released throughout. The right edge is the same
code and behaves identically where it has somewhere to hide into. What follows
is the work still outstanding.

## 1. Top and right edges -- done

Both exercised through the full cycle: hide, reveal, stay-up, re-hide. The
reveal trigger and stay-visible tests were written generically against `ABE_*`,
and no edge-specific geometry bug turned up. The hidden rect for the left edge
is `(-46,0)-(2,1440)` on the primary display -- a 2px sliver.

The right edge is skipped automatically when a display sits beyond it, which is
the configuration on the development machine; see section 2b.

## 2. Show and hide animation -- done

**Solved.** The taskbar rewrites requested window positions in its
`WM_WINDOWPOSCHANGING` handler, forcing every intermediate position to the
endpoint the auto-hide flags imply. The window really was moving; each step was
snapped to the destination before it could be drawn, so the hide looked instant.

The fix is two parts, both shipped:

- Restore the requested position after calling `DefSubclassProc`, so
  intermediate frames survive the clamp.
- Drive the slide from the watch thread rather than through `SlideWindow`, which
  runs its loop on the taskbar's UI thread and blocks the repaint.

Eleven hypotheses were tested and eliminated before this one, including three
confident explanations of my own; the table is in `docs/findings-phase1.md`.
That document's earlier conclusion -- that the slide could not be made visible
because the pixels belong to a Composition island -- was wrong, and is marked as
superseded there.

Remaining: the mod's slide is linear, where Windows uses an eased curve. Driving
the composition visual directly, alongside
`SharedAnimations::TaskbarCollapseAnimation`, would close that gap. It is a
different project, and `ViewCoordinator` is ruled out as an entry point.

## 2b. Known issue: reveal appears to come from the neighbouring display

With a display beyond the docked edge, the hidden position is mostly on that
display -- right-docked on a 3440-wide primary hides to `3438-3486`, and
everything past 3440 is the next monitor. The reveal therefore animates *from*
the neighbour, which reads as the taskbar flying in from the wrong screen.


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
nowhere to hide into. The mod now detects this itself -- `DisplayBeyondEdge`
probes one pixel past the taskbar monitor's boundary -- and stands down on that
edge, which is what "Skip edges facing another display" controls. Anything above
should be read as background, not a plan.

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

