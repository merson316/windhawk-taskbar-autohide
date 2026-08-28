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

## 2. Show and hide animation

The taskbar currently moves without the Windows 11 easing. `SlideWindow` is
passed `animate=1` in both directions, so the flag is not the problem: the
smooth transition is a XAML compositor animation
(`SharedAnimations::TaskbarCollapseAnimation` / `TaskbarExpandAnimation` in
`Taskbar.View.dll`) that is never triggered off-bottom.

Starting points:

- Those two animation functions are the ones `taskbar-auto-hide-speed` hooks, so
  the pattern for reaching them is already proven.
- `ViewCoordinator` was investigated and ruled out -- its hooks bound and never
  fired, because that class implements collapse-to-smaller-size, not auto-hide.
  Do not re-tread it.
- Find what invokes the collapse animation on the bottom edge first, then work
  out why that path is not reached elsewhere. Instrument before theorising: most
  of the time lost on this project went to plausible theories that a five-line
  disassembly would have killed immediately.

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
