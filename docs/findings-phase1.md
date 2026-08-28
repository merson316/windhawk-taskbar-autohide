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

---

# Resolution (2026-08-28, later)

The phase 1 conclusion above was **wrong in an important way**, and it is worth
recording why rather than quietly correcting it.

Phase 1 concluded "the auto-hide flags never become non-zero off-bottom", and
inferred that the *store* was refused. It is not. The store always succeeds.
`TrayUI::GetAutoHideFlags` masks the result:

    cmpl $0x3,0x118(%rbx)     ; edge == ABE_BOTTOM ?
    je   .return_real
    xor  %eax,%eax            ; otherwise report no flags
    ret
  .return_real:
    mov  0x44(%rbx),%eax      ; the genuinely stored flags

That distinction cost three failed attempts, all of which tried to make a store
succeed that was never failing. The getter was named in the very first trace;
disassembling it earlier would have saved most of the investigation.

## Everything the working fix needed

1. Report the stored flags instead of the masked zero.
2. Store `ABS_ALWAYSONTOP` alongside `ABS_AUTOHIDE` off-bottom. `GetDockedRect`
   returns the hidden rect only when `(flags & 3) == 3`; Windows adds the second
   bit itself on the bottom edge but not elsewhere.
3. Suppress `Unhide(request=9)` when the cursor is away. Injecting the hidden
   state prompts an immediate "ensure shown" reflex that undoes the hide.
4. Drive reveal and hide from a timer. Windows' cursor-at-edge detection does
   not run off-bottom, and there is no polling site among the hooks:
   `GetAutoHideFlags` fires in bursts around state changes and not at all while
   the taskbar sits hidden -- one run produced two cursor samples 28 seconds
   apart.
5. Issue the hide slide explicitly. `Unhide` performs its own reveal slide, but
   nothing performs the hide slide off-bottom, so storing the flag changed the
   state and moved nothing.
6. Use two regions: a thin trigger strip to reveal, the full docked rect to stay
   revealed. One tolerance for both hid the taskbar as soon as the pointer moved
   onto it.

## Pointer bases

Three different `this` bases are in play, which caused a crash and a silent
no-op before being pinned down:

| Function | Base | Edge at | Flags at |
|---|---|---|---|
| `SetAutoHideFlags`, `GetDockedRect`, `Unhide`, `SlideWindow` | B | `0x110` | `0x3c` |
| `GetAutoHideFlags` | B - 8 | `0x118` | `0x44` |
| `_ComputeHiddenRect` | B - 0x30 | | |

The V1 driver crashed Explorer by calling `_ComputeHiddenRect` with base B. The
`ALWAYSONTOP` store silently did nothing for one round because it read the edge
at `0x118` from a base-B pointer.

## Ruled out

- `ViewCoordinator` in `Taskbar.View.dll`. The hooks bound and never fired once:
  that class implements collapse-to-smaller-size, not auto-hide.
- The predicate at `Taskbar.dll+0x5CFF4`, which `SetAutoHideState` and
  `SetAutoHideFlags` both call. It returns false on this machine and suppresses
  nothing.
- Spoofing `GetStuckPlace` around either `SetAutoHideFlags` or
  `SetAutoHideState`. Neither consults it for the gate.

## Still open

- No smooth animation: the Windows 11 easing is a XAML compositor animation
  that is never triggered off-bottom.
- Top and right edges are implemented but untested.
- Secondary taskbars remain out of scope.

---

# Animation investigation (2026-08-28)

**Outcome: no defect. The slide animation already works off-bottom.**

Measured on the left edge against a bottom-edge control, with
`taskbar-auto-hide-speed` disabled:

| | Left | Bottom |
|---|---|---|
| Slide duration | 406 / 421 / 406 ms hide, 219 ms show | 407 / 422 / 422 ms hide, 219 ms show |
| SetWindowPos steps | 13 hide, 7-8 show | 13 hide, 7 show |
| Step spacing | ~31 ms | ~31 ms |
| Step flags | `0x15` (NOSIZE\|NOZORDER\|NOACTIVATE) | `0x15` |
| Interpolation | smooth, x -3 -> -43 | smooth, y 1431 -> 1410 |
| `animate` argument | 1 | 1 |
| Durations at this+0x168 / +0x16c | 400 / 200 | 400 / 200 |
| `SPI_GETMENUANIMATION` | 1 | 1 |
| Animation gate (vtable+0x360) | 1 | 1 |

The perceived difference is geometric: both edges travel 46 px in the same time,
but a horizontal taskbar sweeps 3440 px of screen width while a vertical one
sweeps 48 px. The same motion over far less area reads as much faster and is
easy to mistake for an instant vanish.

A `hideDurationMs` / `showDurationMs` setting was added so the vertical slide can
be lengthened to taste. It writes this+0x168 / +0x16c, guarded by a signature
over the two load instructions inside `SlideWindow`, which encode both offsets.

## Hypotheses killed, in order

1. **Compositor animations never fire off-bottom.** Wrong target:
   `TaskbarCollapseAnimation` and friends construct the animation object once
   and are not called per transition, so their silence meant nothing.
2. **The duration is zero off-bottom.** Identical, 400 / 200 on both edges.
3. **The window is already at the target, so there is nothing to animate.**
   Rects differ by 46 px, `same=0` on every slide.
4. **The `vtable+0x360` gate returns zero off-bottom.** Returns 1 on both.
5. **The animation is skipped despite the gates.** Slide duration matches the
   configured animation time exactly, so the loop runs to completion.

Each was killed in a single test cycle, because every run had a bottom-edge
control to compare against. That is the practice worth keeping.

## Notes for future work here

`SlideWindow`'s animation is a synchronous timed loop on the calling thread:
`GetCurrentThread` / `GetThreadPriority` / `SetThreadPriority` to raise
priority, then `GetTickCount` with `SetWindowPos` steps. It is not a compositor
animation, so DWM-level approaches do not apply.

## The slide blocks the shell

`SlideWindow`'s animation is a synchronous loop on the taskbar's UI thread:
raise thread priority, then step the window with `SetWindowPos` against
`GetTickCount` until the duration elapses. The mod calls it from the watcher, so
**the animation duration is also how long the taskbar is frozen**.

Observed directly by varying `hideDurationMs`:

| Setting | Behaviour |
|---|---|
| 3000 ms | Taskbar vanishes without repainting, then ignores the pointer for 3 s |
| 25 ms | Reveal is instant |

The "vanish" is the absence of repaints while the thread is blocked, and the
unresponsive period is the block itself. This makes the duration knob
self-limiting, so it is capped at 600 ms. A longer, genuinely smooth animation
would require driving the movement asynchronously rather than calling
`SlideWindow`, which is a much larger change and is not worth it: the animation
already matches the bottom edge exactly at default settings.

## WM_TIMER starvation

The watcher originally ran on `WM_TIMER`, which Windows only delivers when the
thread's message queue is empty. Hovering the taskbar generates a continuous
stream of mouse messages, so the timer was starved precisely when the reveal
needed to fire -- the reveal felt sticky while the hide, with the pointer away
and the queue quiet, always felt responsive. Replaced with a worker thread
posting an ordinary registered message, which is not deprioritised, while the
work still runs on the taskbar's thread.

## Why the duration setting has no visible effect

Verified with `hideDurationMs: 600`:

    dur(h=600,s=150)
    show=0 -> took 625 ms, 20 SetWindowPos steps,
              (0,0)-(48,1440) -> (-46,0)-(2,1440)

The override lands, and the window slides smoothly for the full duration. But
25 ms and 600 ms look identical, because **the taskbar's rendered content
disappears at the state change, before the slide begins**. The window then
animates while already empty, so its duration cannot be seen.

That content is drawn by the XAML layer. `SlideWindow` moves the window, not the
pixels, so no amount of tuning here changes what is visible.

One untried idea, recorded rather than pursued: set the flags to obtain the
hidden rect, set them back to "shown" so the content keeps rendering, run the
slide, and only then mark it hidden. Whether the content survives that thrash is
unknown, and it risks flicker and the request=9 unhide reflex.

The durations remain useful for one thing: a low value (25 ms) makes the hide
effectively instantaneous, which removes the shell freeze described above and
makes the reveal feel immediate.

## Notification flapping (fixed)

Windows forces an auto-hidden taskbar visible when an app flashes its taskbar
button. The watcher re-hid it immediately, so it strobed: unhide, hide, unhide.

Fixed by holding it visible for 4 s after any unhide the mod did not issue.
Verified with a Discord notification against a left-docked taskbar:

    14:45:16  show=1 -> took 156 ms
    14:45:34  show=0 -> took 407 ms

One show, one hide, 18 s apart, where previously it cycled.

Note that `Better Taskbar Autohide` (installed here, 1500 ms delay) encodes the
opposite intent -- it permits hiding *despite* a notification -- but works
through `CTaskListWnd::PermitAutoHide`, which sits on Windows' own hide path.
That path does not run off-bottom, so the two do not currently conflict. If the
hold ever needs to match that behaviour, the value to change is HOLD_VISIBLE_MS.

### Reproducing

`tools/flash-taskbar.ps1` flashes a taskbar button. Two traps found while
writing it: `FlashWindowEx` is silently ignored while the target window is in
the foreground, so the window must be minimised first; and Windows 11 Notepad is
a packaged app whose `MainWindowHandle` goes stale after minimising, so it does
not work as a target. Use `-Process Discord` or another normal app. Toast
notifications do not force an auto-hidden taskbar visible at all.
