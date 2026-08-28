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

## 2. Show and hide animation - CLOSED, blocked by design

The window slide itself works off-bottom and is configurable. What cannot be
done through this path is making it *visible*, and that is now demonstrated
rather than assumed:

- Setting the hidden flag before the slide drops the rendered content, so an
  empty window animates.
- Setting it after the slide leaves the window unable to stay off-screen --
  measured, it completes 13 steps toward the hidden rect and finishes back at
  the docked rect, because Windows enforces the position the flags imply.

The flag is both required for the move and fatal to the visuals. Reopening this
means keeping the content drawn while the flag says hidden, which is the XAML
layer in `Taskbar.View.dll`, not `SlideWindow`. `ViewCoordinator` is ruled out.

`src/taskbar-autohide-anim-experiment.wh.cpp` preserves the second test. See
`docs/findings-phase1.md`.

## 3. Secondary monitor support

Currently out of scope, and `MMTaskbarEnabled` is `0` on the development
machine, so nothing exercises it.

Secondary taskbars are a separate class with their own copies of the same
machinery:

    CSecondaryTray::_AutoHide(bool)
    CSecondaryTray::_ComputeHiddenRect(struct tagRECT *)
    CSecondaryTray::Unhide(enum TrayCommon::UnhideRequest)
    CSecondaryTray::GetStuckPlace(unsigned long *)

Note the different signatures -- `GetStuckPlace` takes an out-parameter rather
than returning the edge, so the pointer-base assumptions from `TrayUI` do not
carry over. Re-derive them.

The timer watcher currently tracks a single `g_trayUI` captured from
`GetDockedRect`. Multi-taskbar support means tracking one state per taskbar
rather than one global.

## Working practices that paid off

- Measure before theorising. `GetWindowRect` and cursor logging each overturned
  a confident theory in one test cycle.
- Compile and link locally with `tools/whcompile.sh` before pasting into
  Windhawk. A syntax-only check once passed a build that could not link.
- Truncate logs with `tools/clearlog.sh`; never delete them. The mod holds the
  file open, so an unlink loses the whole run.
- Check the PDB's date before mining it. One round of symbol names came from a
  build two months stale.
