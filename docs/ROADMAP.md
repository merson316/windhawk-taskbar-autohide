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

A display beyond the edge is handled since 2.0.0; see section 2b.

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

Since 2.0.0 the slide eases into its end position and each taskbar steps at
its own display's refresh rate.

## 2b. An edge with a display beyond it -- done in 2.0.0

With a display beyond the docked edge, the hidden position is on that display.
Windows clips the hidden window there, and the window takes that display's
scaling (issue #11: icons the wrong size after a reveal, with displays scaled
differently). Two earlier attempts are recorded in the history of this file
(`git log -p docs/ROADMAP.md`): one moved and resized the window and ended in a
loop between three positions; the other declined the edge, and failed because
"auto-hide is on, but not on this edge" is a state Windows has no concept of.

What works: the window never leaves its shown position. Its content window
(`Windows.UI.Composition.DesktopWindowContentBridge`) is slid inside it and a
window region keeps only the visible band. Nothing is drawn on the other
display, and the window's scaling never changes. The taskbar is revealed from
a strip on the edge of its own display.

## 3. Secondary taskbars -- done in 2.0.0

Every taskbar has a record of its own: its display, scaling, refresh rate,
edge and animation. Off the bottom edge the mod hides and reveals the secondary
taskbars itself; on the bottom edge Windows decides and the mod carries the
movement out. `docs/taskbar-internals.md` and
`docs/taskbar-internals-outside.md` record what was read out of `taskbar.dll`
and the components around it.

## 4. Outstanding

- **Windows' own hide logic on every edge.** One feature check
  (`Feature_61970341`) gates both edge tests in `taskbar.dll`. Answering it
  would let Windows decide the hiding off the bottom edge as it does on it,
  and retire the mod's own pointer tracking. Not tried.
- **One animation path.** On the bottom edge with "Hide completely" off and
  nothing held, the slide is still Windows' loop, paced through hooks on
  `Sleep` and `GetTickCount`. The
  mod's own animation could take it over in every layout and those two hooks
  could go.
- **Layouts not yet run through `tools/test-taskbars.ps1`:** displays side by
  side, three or more displays, displays that share only part of an edge.
