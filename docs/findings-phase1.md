# Phase 1 diagnostic findings

Date: 2026-08-28
Build: 10.0.26300.9278 (26H2), Taskbar.dll at 00007FFFE3950000
Instrumentation: 14/14 symbols resolved

Traces: `research/trace-left.log` (broken, 97 lines),
`research/trace-bottom.log` (working control, 910 lines).

## Result

**Spec case 1 — the auto-hide flags never become non-zero on a non-bottom
edge.** Everything downstream is gated on those flags, so no hide is ever
attempted.

## Evidence

Left edge, auto-hide toggled on:

    SetAutoHideState(1) ENTER
    GetAutoHideFlags -> 0x0
    SetAutoHideFlags(0x1)  ENTER [edge=0 LEFT]
    GetAutoHideFlags -> 0x0            <-- unchanged
    SetAutoHideFlags(0x1)  LEAVE
    SetAutoHideState(1) LEAVE -> 0x00010001    <-- reports success
    GetDockedRect(0) -> 0x0 (0,0)-(48,1440)

Bottom edge, identical action:

    SetAutoHideState(1) ENTER
    GetAutoHideFlags -> 0x0
    SetAutoHideFlags(0x1)  ENTER [edge=3 BOTTOM]
    GetAutoHideFlags -> 0x1            <-- takes effect immediately
    SetAutoHideFlags(0x1)  LEAVE
    SetAutoHideFlags(0x3)  ENTER [edge=3 BOTTOM]   <-- never happens on left
    GetAutoHideFlags -> 0x3
    SetAutoHideFlags(0x3)  LEAVE
    _ComputeHiddenRect(edge=3 BOTTOM) -> (0,1438)-(3440,1486)
    GetDockedRect(0) -> 0x3 (0,1438)-(3440,1486)
    SlideWindow show=0 animate=1 [edge=3 BOTTOM]

Return values of `GetAutoHideFlags` across each whole trace:

| Value | Left | Bottom |
|---|---|---|
| `0x0` | 64 | 267 |
| `0x1` (`ABS_AUTOHIDE`) | 0 | 92 |
| `0x3` (`+ ABS_ALWAYSONTOP`) | 0 | 221 |

`_ComputeHiddenRect`, `SlideWindow` and `_AutoHideCollision` were called 0 times
on the left edge and 28+ times at the bottom.

## Mechanism

`SetAutoHideFlags(0x1)` is invoked on the correct object with the correct edge
on both edges — the request reaches `TrayUI` intact. The difference is that on
the bottom edge the value becomes visible to `GetAutoHideFlags` immediately and
triggers a follow-up `SetAutoHideFlags(0x3)`; on a non-bottom edge the flags
stay `0x0` and the follow-up never occurs.

The precise internal reason (an early-out inside the store, or an edge-gated
propagation step between the storing object and the object the getter reads) was
not determined, and does not need to be in order to act: the actionable
observable is that the flags never become non-zero off-bottom.

## What already works off-bottom

Notable, because it removes the most expensive contingency from the spec:

- `GetDockedRect(0)` returns correct left-edge geometry `(0,0)-(48,1440)`.
- `SetUnhideTimer(20, 452)` is armed.
- `Unhide(flags=2 request=1) [edge=0 LEFT]` fires on cursor sweeps into the
  left edge.

The unhide trigger machinery therefore already works on a vertical edge. Spec
case 4 — building our own hover detection — is not needed.

## Instrumentation caveats

- `GetAutoHideFlags` and `SetAutoHideState` are reached through an adjustor
  thunk (`?GetAutoHideFlags@TrayUI@@W7EAAIXZ` exists in the PDB), so `pThis` in
  those hooks is offset and the logged `edge=48` is meaningless there. Calls
  logging a real edge (`SetAutoHideFlags`, `_ComputeHiddenRect`, `SlideWindow`,
  `Unhide`) use the unadjusted pointer and are trustworthy.
- `WindhawkUtils::HookSymbols` requires undecorated names with a trailing space
  where `__ptr64` was stripped. Const methods fail to resolve without it.

## Recommended phase 2 direction

Make the flags read as enabled on non-bottom edges, then let Windows' own
machinery do the rest. Two candidate mechanisms, to be settled in phase 2
planning:

1. Hook `GetAutoHideFlags` to return the intended flags when the taskbar is
   docked off-bottom and auto-hide is enabled. Simple, but makes the getter
   disagree with stored state.
2. Spoof `GetStuckPlace` as `ABE_BOTTOM` for the duration of the
   `SetAutoHideFlags` call only, via a thread-local flag, so the real store
   succeeds. Narrower lie, but depends on the gate actually consulting
   `GetStuckPlace` rather than reading a member directly — which is not yet
   established and should be tested first.
