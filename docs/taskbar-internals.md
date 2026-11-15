# taskbar.dll internals: auto-hide, position, z-order

Reference for what `taskbar.dll` 10.0.26100.9974 does. Written on 2026-11-15
from the disassembly and Ghidra pseudocode of that one build, with names from
its published PDB. Build 10.0.26100.10066 was not read for this document.

Every statement outside an "Inferred, not observed" list was read in the code.
Addresses are RVAs. "Earlier dig" marks facts taken from the first REA session
of the same day and not read again.

## Conventions

`TrayUI` has several vtables. Three `this` values are in use.

| Name here | Pointer | Used by |
|---|---|---|
| base 0 | the object | private methods: `_Hide`, `_UnhideNow`, `_ShouldAutoHideTray`, `_ResetZorder`, `_HandleWindowPosChanging` |
| base +0x28 | object + 0x28 | `WndProc`, `OnTimer`, `GetAutoHideFlags` (0x41f70), `SetAutoHideState` (0xa6720), `RefreshSettings` |
| base +0x30 ("B" in the mod) | object + 0x30 | `GetStuckPlace`, `SetAutoHideFlags`, `GetDockedRect`, `Unhide`, `SlideWindow`, `SetStuckPlace` |

`GetAutoHideFlags` 0xf4690 and `SetAutoHideState` 0xffca0 are thunks on base
+0x30. They subtract 8 and jump to the real function.

Members, as offsets from base 0 (subtract 0x30 for the mod's base B):

| Offset | Meaning |
|---|---|
| +0x54 (byte) | set to 1 by `_Hide` and `_UnhideNow` around their slide |
| +0x55 (byte) | `Unhide` acts only when this is non-zero |
| +0x68 | auto-hide setting bit (B+0x38) |
| +0x6c | auto-hide flags (B+0x3c). Bit 0: auto-hide on. Bit 1: hidden |
| +0x80 | a child window. Receives 0x439, 0x465, 0x46c, 0x46e |
| +0xa8 | four stuck rects, indexed by edge (B+0x78) |
| +0xe8 | a mode value. `_Hide` and `_UnhideNow` do nothing when it is 2 |
| +0xf0 | non-zero during a size/move loop |
| +0x108, +0x10c | last point given to `SetUnhideTimer` |
| +0x110 | count of open tray flyouts |
| +0x140 | edge (B+0x110). +0x14c: edge during a move, or -1 (B+0x11c) |
| +0x160 | the taskbar's monitor (B+0x130) |
| +0x188 | nesting count of `InvisibleUnhide` |
| +0x198, +0x19c | slide duration in ms, hide and show (B+0x168, B+0x16c) |
| +0x1f8, +0x1f9, +0x1fa | UI-visible bytes read by `_ResetZorder`. +0x1f9 is the hover UI |
| +0x1fb, +0x1fc | cloak requests: gaming full screen; until next render |
| +0x210 | monitor on which Start is visible, or 0 |
| +0x218 | monitor on which a flyout is visible, or 0 |
| +0x258 | the host object. Slot 0x18 returns the taskbar window |
| +0x2a8 | `TaskbarHost`, the owner of the content child window |

Private window messages of `Shell_TrayWnd` (`TrayUI::WndProc`, 0x18c10):

| Message | Effect |
|---|---|
| 0x5c3 | announces the edge: `ImmersiveTray::NotifyTrayStuckPlaceChanged` (0xa5a84) |
| 0x5c5 | `_ResetZorder(0)`, then `_SetAutoHideTimer`. Sent by `ImmersiveTray::RaiseWindow` |
| 0x5ca | `_HandleTrayPrivateSettingMessage(wParam, lParam)` |
| 0x5d1 | a flyout became visible (wParam) on a monitor (lParam) |

Timers on `Shell_TrayWnd` (`TrayUI::OnTimer`, 0x9c580):

| Id | Period | Effect |
|---|---|---|
| 2 | 500 ms, repeating | asks `_ShouldAutoHideTray`, then calls `_Hide` |
| 3 | 50 ms, one shot | unhide if the pointer is inside the window |
| 0xc | | `SetForegroundWindow` on the taskbar |
| 0x1e | 1000 ms by default | ends "cloak until next render" |

## Edge change

`TrayUI::_HandleTrayPrivateSettingMessage` (0x103584) handles message 0x5ca.
The setting number is wParam.

| Setting | Effect |
|---|---|
| 1 | `_UpdateTaskbandVisibility`, then 0x46a to the child window |
| 2 | calls slot 0x178 of the base +0x28 vtable. Not read further |
| 3 | returns the auto-hide setting bit (+0x68) |
| 4 | stores the auto-hide setting bit, then applies it (see next section) |
| 5 | returns `GetStuckPlace()` |
| 6 | moves the taskbar to edge lParam |
| 7 | returns `GetMultimonStuckPlace()` (0xf98a0) |

Settings 3, 4 and 6 are sent from inside `taskbar.dll` by the WinRT type
`WindowsUdk.UI.Shell.TaskbarSettings`: `get_HideTaskbar` (0x141110) sends 3,
`put_HideTaskbar` (0x141950) sends 4 and `Location` (0x13ff68) sends 6. The
code after `put_HideTaskbar` (0x1419d0) sends setting 10, which this handler
ignores.

Setting 6 does this, in order (earlier dig, confirmed here):

1. Nothing, if the edge is unchanged and multi-monitor taskbars are off.
2. If `(GetAutoHideFlags() & 3) == 3`: `SetAutoHideState(0)`.
3. `SetStuckPlace(edge)` (0x100220): stores the edge, posts 0x5c3,
   `_CloakUntilNextRender` (0x102660), `SetWindowPos`, `_ClipWindow`,
   `_StuckTrayChange` (0x9c4cc). It sets +0xf0 around the `SetWindowPos`.
4. If step 2 ran: `_UpdateAutoHideState(1)` (0x103f68), which is
   `SetAutoHideState(1)`.
5. `Unhide(0, 7)`, `SizeWindows()`.

`_CloakUntilNextRender` cloaks the window, starts timer 0x1e and queues a
callback on the core window's dispatcher. The timer period comes from
`GetStuckPlaceChangeRerenderTimeoutMs` (0x1362d8): a registry value, 1000 ms
when absent.

`TrayUI::ApplyRemoteSettings` (0xf3ba0) is a second path. It sets the edge
and the auto-hide state from one packed value, then calls `Unhide(0, 0xc)`
and `SizeWindows()`. Its caller is outside `taskbar.dll`.

Inferred, not observed:

- The consumer of the 0x5c3 announcement is outside `taskbar.dll`. How long
  it needs is not known. The only wait Windows itself uses here is the
  1000 ms re-render timeout.
- Which registry value overrides the timeout was not read.

## Auto-hide state and flags

Two functions test the edge. Both tests are under the same feature flag,
`Feature_61970341`. Its check function (0xbb674) has exactly two callers.

- `TrayUI::GetAutoHideFlags` (0x41f70): returns the flags at +0x6c. With the
  feature on and the edge not 3 (bottom) it returns 0.
- `TrayUI::_Hide` (0x5e344): with the feature on and the edge not 3 it
  returns at once.

Nothing else in `taskbar.dll` uses that feature flag.

`TrayUI::SetAutoHideFlags` (0x3cc90) stores the flags at +0x6c and stores
`flags & 1` at +0x68 on every call. It then sets the window property
`IsAutoHideEnabled` on the taskbar window to `GetAutoHideFlags() & 1`. When
`IsExpandable()` is true it stores 0 for the flags and 1 for the property.

Callers of `SetAutoHideFlags` and the value each stores:

| Caller | Value |
|---|---|
| `_Hide` | 3 |
| `Unhide` | 1 |
| `SetAutoHideState`, turning on | 1 |
| `SetAutoHideState`, turning off | 0 |
| `SetAutoHideState`, no change or registration failed | `GetAutoHideFlags()` |
| `_KickStartAutohide` | 3, or 0 |

`TrayUI::SetAutoHideState(on)` (0xa6720):

1. `on` becomes 0 when `IsExpandable()`.
2. If `on` equals `GetAutoHideFlags() & 1`: store `GetAutoHideFlags()` again
   and return 0x10000.
3. Ask the host to register or unregister the auto-hide bar for this window,
   edge and monitor (host slot 0x78). On failure: store `GetAutoHideFlags()`
   again, call `_AutoHideCollision` (0x102400), return 0.
4. Turning on: store 1, `RefreshSettings`, and if `_ShouldAutoHideTray` says
   yes, `_Hide`.
5. Turning off: kill timer 2, store 0, `_UnhideNow(animate)`,
   `RefreshSettings`.
6. `_StuckTrayChange`, return 0x10001.

`TrayUI::_KickStartAutohide` (0xd9a94) runs once from `TrayUI::Initialize`
(0x6aec0). If `GetAutoHideFlags() & 1`: store 3, `Unhide(0, 0xb)`, register
the auto-hide bar, and on failure `SetAutoHideState(0)`. Otherwise: store 0.

Consequence, off the bottom edge with the feature on and no hook: at every
Explorer start `GetAutoHideFlags()` is 0, so `_KickStartAutohide` stores 0.
That clears both the flags and the setting bit.

`TrayUI::_GetSaveStateAndInitRects` (0x5ee40) loads the setting bit at start
and copies it to the flags, unless the host reports a mode in which it uses
0. `TrayUI::SaveTrayStuff` (0x9ceb0) saves `(setting bit) | 2`. That is the
byte seen as 02 or 03 in `StuckRects3`.

Other readers of the setting bit: `GetDesktopModeAutoHide` (0x95a50, a
two-instruction getter on base +0x30), `GetTrayViewOpts`, `SetTaskbarModel`,
and `_HandleSettingChange` (0x67a18). The last one runs
`SetAutoHideState(setting bit)` when `WM_SETTINGCHANGE` carries the string
`TraySettings`, `ConvertibleSlateMode` or
`SearchBoxTouchImprovementSettingChange`.

`TrayUI::_OnSettingChange` (0x67c74) with wParam `SPI_SETWORKAREA` only calls
`UpdateDisplayMonitorInfoCollectionWorkAreas` (0x67320).

Inferred, not observed:

- "Auto-hide reads off for about a minute after Explorer starts" fits
  `_KickStartAutohide` running before the mod's hook was in place. It was not
  watched happening.
- What turns it on again later is outside `taskbar.dll`. Candidates in the
  code: setting 4 of message 0x5ca, `ApplyRemoteSettings`, and the appbar
  message handler `HandleCopyData` (0x28da0).
- Who reads the `IsAutoHideEnabled` property is not in `taskbar.dll`.

## The primary's hide and unhide

### What starts the hide timer

`TrayUI::_SetAutoHideTimer` (0x1a5f4): if `GetAutoHideFlags() & 2`, kill
timer 2. Else if bit 0 is set, start timer 2 at 500 ms. Callers: `Unhide`,
`WndProc` (on `WM_ACTIVATE` with wParam 0, on message 0x212, on 0x5c5, on
0x5d1), `HandleTrayFlyoutVisibilityChange` (0xfa71c), `OnNextCtl`,
`_HandleImmersiveInvoke`.

Timer 2 repeats. `_Hide` kills it.

### What Windows checks before hiding

`TrayUI::_ShouldAutoHideTray` (0x4ba7c) answers no in each of these cases,
tested in this order:

1. A size/move loop is running (+0xf0).
2. Auto-hide is off (flags bit 0 clear).
3. Already hidden (flags bit 1 set).
4. `BandSite_PermitAutoHide` (0x4caa4) says no. It queries the band site for
   command 7. See "Notifications and PermitAutoHide".
5. Start is visible on the taskbar's monitor: `GetLauncherVisibleMonitor()`
   (0xa0ea0, returns +0x210) equals +0x160.
6. A flyout is visible on the taskbar's monitor: +0x218 equals +0x160.
7. `_IsActive` (0x91208): the foreground window is the taskbar, or a window
   owned by the taskbar, or the flyout count at +0x110 is above 0.
   `HandleTrayFlyoutVisibilityChange` raises and lowers that count.
8. A view coordinator object exists (+0x400) and its slot 0x78 returns 0.
9. The pointer is inside the stuck rect for the current edge, inflated by
   four times two global metric values, and `IsCursorSuppressed` (0x653b0) is
   false.
10. The child window at +0x80 returns non-zero for message 0x439.

Otherwise it answers yes. It has two callers: `OnTimer` (timer 2) and
`SetAutoHideState`.

The function takes a telemetry activity object. It stops and destroys that
object before it returns. It cannot be called with a null or reused argument.

### The hide

`TrayUI::_Hide` (0x5e344), after the edge test:

1. Return if +0xe8 is 2.
2. Kill timer 2.
3. Set +0x54 to 1.
4. `SetAutoHideFlags(3)`.
5. `GetDockedRect(&rect, 0)`. This now gives the hidden rect.
6. `SlideWindow(hwnd, &rect, NULL, show = false, animate = true)`.
7. `_ReportAutoHideStateToChildControls(GetAutoHideFlags())` (0x5e87c).
8. Set +0x54 to 0. `SetAccOffScreen`.

Callers of `_Hide`: `OnTimer` (timer 2), `SetAutoHideState`,
`InvisibleUnhide`, and `OnLauncherVisibilityChange`.

`TrayUI::OnLauncherVisibilityChange` (0x8e830) runs when Start opens or
closes. When Start is visible on the taskbar's monitor and auto-hide is on it
calls `Unhide(1, 2)`. When Start is not, auto-hide is on, no flyout is on the
monitor and slot 0x498 returns false, it calls `_Hide` directly. It does not
ask `_ShouldAutoHideTray` first.

### The unhide

`TrayUI::Unhide(flags, request)` (0x5e670):

1. If +0x55 is non-zero and `(GetAutoHideFlags() & 3) == 3`:
   `SetAutoHideFlags(1)`, `_UnhideNow(animate = !(flags & 1))`,
   `ClearAccOffScreen`, and `_SetAutoHideTimer` unless `flags & 2`.
2. Always: `_ReportAutoHideStateToChildControls(GetAutoHideFlags())`.

`TrayUI::_UnhideNow(animate)` (0x5e508): return if +0xe8 is 2. Set +0x54.
Report to child controls. `SlideWindow(hwnd, &stuckRect[edge], NULL,
show = true, animate)`. Clear +0x54.

The request value is passed to telemetry only. Call sites in this build:

| Flags | Request | Caller |
|---|---|---|
| 1 | 2 | `OnLauncherVisibilityChange`: Start opened |
| 3 | 4 | `HandleTrayFlyoutVisibilityChange`: first flyout opened |
| 0 | 5 | setting 4 of 0x5ca with value 0: auto-hide switched off |
| 0 | 7 | setting 6 of 0x5ca: edge change |
| 0 | 8 | `WndProc`: activation |
| 1 | 9 | `InvisibleUnhide` |
| 0 | 0xb | `_KickStartAutohide` |
| 0 | 0xc | `ApplyRemoteSettings` |
| 0 | 0xf | `OnTimer`, timer 3: the pointer |
| 0 | 0x11 | `CTaskBand::UnhideTray` (0x5e620): a flashing button |
| 1 | 0x12 | `WndProc`, message 0x5d1: flyout on this monitor |

No call with request 1 was found in this build.

### The pointer reveal

`WndProc` on `WM_NCHITTEST` calls `SetUnhideTimer(x, y)` (0x41bc0). If
`(GetAutoHideFlags() & 3) == 3` and the point is further from the last stored
point than the double-click rectangle, it starts timer 3 at 50 ms and stores
the point.

Timer 3: kill the timer, reset the stored point, and if flags bit 1 is set,
the real pointer is inside `GetWindowRect` of the taskbar and
`IsCursorSuppressed` is false, call `Unhide(0, 0xf)`.

This path has no edge test of its own. It depends only on `GetAutoHideFlags`.

### InvisibleUnhide

`TrayUI::InvisibleUnhide(end)` (0x5e14c) brackets layout work while the
taskbar is hidden. Callers: `SizeWindows` (0x3c060, only when flags bit 1 is
set), `VerifySize`, `_HandleSizing`, `_OnThemeChanged`.

- Begin (first level): `_ClipWindow(1)`, `WM_SETREDRAW` off, `ShowWindow`
  hide, `Unhide(1, 9)`.
- End (last level): `_Hide()`, `ShowWindow(SW_SHOWNA)`, `WM_SETREDRAW` on.

So request 9 is always followed by a `_Hide`. Off the bottom edge that
`_Hide` returns at the edge test, and the taskbar stays unhidden.

### SlideWindow

`TrayUI::SlideWindow(hwnd, rect, monitor, show, animate)` (0x41260):

1. Return at once if the window is not visible. Nothing else happens.
2. Work out a clip rect from the monitor, or from `_ShouldClipTaskbar`.
3. Duration: +0x19c when showing, +0x198 when hiding.
4. Animate only if `animate`, the duration is above 0, `GetDragFullWindows()`
   is true and `SPI_GETMENUANIMATION` is on.
5. The loop: raise the thread priority to 2. Read `GetTickCount`. Until the
   duration has passed: when the tick value has changed, move the window with
   `_SlideStep` (0x5f664), and `UpdateWindow` when showing. Then, if less
   than 20 ms passed in this step, `Sleep` for the rest of the 20 ms. When
   hiding, also `SendMessageTimeout` 0x45d to the desktop window with a 50 ms
   limit. Restore the priority.
6. One last `_SlideStep` to the target rect. `UpdateWindow` when showing.
7. `AutoHideChangedOnMonitor` (0xb4b7c) for the given monitor, or for the
   taskbar's own monitor when the window is the primary's.

`_SlideStep` calls `SetWindowPos(hwnd, 0, x, y, 0, 0,
SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)` and updates the clip region.

Secondary taskbars slide through this same function (see below).

While the loop runs, other code runs on the same thread: the handlers of
`SetWindowPos`, `UpdateWindow`, and any message sent from another thread
while `SendMessageTimeout` waits.

Inferred, not observed:

- With the edge test lifted, steps 1 to 8 of `_Hide` would run on every
  edge. `_ComputeHiddenRect` and `SlideWindow` contain no edge test, and a
  left-edge hidden rect was measured in phase 1. The whole path was not run
  off the bottom edge.
- The meaning of +0x55, of +0xe8 and of host slot 0x498 was not read.
- The child window at +0x80 is probably the notification area. Not read.
- Whether Quick Settings reports itself through message 0x5d1 is decided
  outside `taskbar.dll`. No sender of 0x5d1 exists in it.

## Window position

`TrayUI::GetDockedRect(rect, arg)` (0x41ed0): the edge is +0x14c when `arg`
is non-zero and that member is not -1, else +0x140. The rect is the stuck
rect for that edge. If `(GetAutoHideFlags() & 3) == 3` it is replaced by
`_ComputeHiddenRect` (0x41fa8). The function returns the edge.

`_ComputeHiddenRect` moves the rect past the display edge and leaves half of
one global metric value on screen. That was measured as 2 px.

`TrayUI::GetStuckPlace` (0x65250) returns +0x14c unless it is -1, else +0x140.

`TrayUI::_HandleWindowPosChanging` (0x41ca4) handles `WM_WINDOWPOSCHANGING`:

1. If a move is in progress (+0x14c not -1) and the request has neither
   `SWP_NOMOVE` nor `SWP_NOSIZE`: `_DoneMoving`.
2. If no size/move loop is running and +0x54 is set: leave the request alone.
3. Otherwise read `GetWindowRect` and `GetDockedRect`.
   - If the two are equal: add `SWP_NOMOVE | SWP_NOSIZE` to the request.
   - If not: overwrite x, y, cx, cy with the docked rect and clear both flags.
4. If the edge or the monitor changed during the call: post 0x5c3.

Step 3 has a consequence for any other caller of `SetWindowPos`. While the
window sits exactly on the docked rect, every move request comes back with
`SWP_NOMOVE` set. A handler that runs after this one and tests `SWP_NOMOVE`
sees a request that asks for nothing.

Callers of `GetDockedRect`: `_HandleWindowPosChanging`, `_Hide`,
`CTaskBar::OnPosRectChangeDB` (0x42110), `_ShouldClipTaskbar`,
`_RestoreWindowPos`, `_HandleSize`.

Inferred, not observed:

- The sliver size at other scale factors. The code divides a metric by 2. It
  was 2 px at 96 and at 120 DPI.
- `CSecondaryTray::_OnWindowPosChanging` (0x94948) was not read. It is not
  known whether it has the same "equal means NOMOVE" rule.

## Secondary taskbars

Each secondary taskbar is a `CSecondaryTray` with window class
`Shell_SecondaryTrayWnd`. It keeps its own flags (+0xd0), edge (+0x40),
monitor (+0x48) and stuck rects (+0x78).

Earlier dig:

- The flags are loaded from `TrayUI::GetAutoHideFlags() & 1` at creation, in
  `RefreshSettings` (0x93bc0) and in `SyncDisplayChange` (0x160e60).
- `_OnTimer` (0x344c8), timer 2 at 500 ms, hides unless one of several
  conditions holds. It exits when the hidden bit is already set.
- `_OnNCHitTest` (0x9c180) starts timer 3 at 50 ms when the flags are 3. The
  timer calls `_Unhide(0, 0xf)` if the real pointer is inside the window.
- `_Unhide(flags, request)` (0xbae8c) does nothing unless the hidden bit is
  set. Flags bit 0: no animation. Bit 1: do not start the hide timer again.

Read in this audit:

- `CSecondaryTray::_AutoHide(hide)` (0x161368). With `hide` true: kill timer
  2, set both flag bits, compute the hidden rect with its own
  `_ComputeHiddenRect` (0xda1ec), call `TrayUI::SlideWindow(hwnd, &rect,
  monitor, false, true)`, then `_UpdateChildControls_SetAutoHide` (0x161c8c)
  and `SetAccOffScreen`. With `hide` false: start timer 2 at 500 ms.
- `_AutoHide` has no edge test. The secondary's hide works on any edge once
  its flags have bit 0 set.
- Callers of `_AutoHide`: `v_WndProc`, `_OnTimer`, `CheckSize`, `_Unhide`,
  `SetStuckPlace` (0x160ba0), `_HandleImmersiveInvoke`,
  `_HandleTrayFlyoutVisibilityChange`, `_UpdateAutoHideBar` (0x161b78).
- `CSecondaryTray::Unhide(request)` (0xa8ac0) jumps to `_Unhide` with flags
  0. Its one caller is `CSecondaryTaskBand::UnhideTray` (0xa8a80), which
  passes request 0x11.
- It has its own `_ResetZorder` (0xada44) and `_UpdateCloakState`
  (0x161cd0).

Inferred, not observed:

- "Windows keeps one edge for all taskbars." The code keeps one per tray and
  has `TrayUI::GetMultimonStuckPlace` and setting 7. Whether the two can
  differ in this build was not read.
- What makes `RefreshSettings` run for a secondary, other than a settings
  change, was not traced.

## Notifications and PermitAutoHide

A flashing taskbar button asks for the taskbar through
`CTaskListWnd::RedrawTask` (0x2cf40), which calls `UnhideTray` on its band.
`CTaskBand::UnhideTray` (0x5e620) calls `TrayUI::Unhide(0, 0x11)`.
`CSecondaryTaskBand::UnhideTray` calls `CSecondaryTray::Unhide(0x11)`.
Request 0x11 has no other source.

`CTaskListWnd::PermitAutoHide` (0x35c40) returns FALSE in each of these
cases:

| Reason code | Case |
|---|---|
| 1 | the task-list flyout held at +0x240 is open |
| 5 | with the hover-cards feature: the flyout at +0x250 is open |
| 2 | the byte at +0x1d0 is set. `ShowJumpView` (0x169a20) uses it |
| 3 | any button group answers `CTaskBtnGroup::IsFlashing` (0x36d60) |
| 4 | the flyout at +0x248 is open |

The three flyout objects are made in `CTaskListWnd::CreateFlyout`
(0x1657d0).

So this function is not only the flashing veto. It is also what keeps the
taskbar up while a task-list flyout or a jump list is open.

Callers: `CTaskListWndMulti::PermitAutoHide` (0x35bb0), which also asks
every other task list, and `CTaskListWnd::ShouldStayExpanded` (0x1699b0).
`_ShouldAutoHideTray` reaches it through `BandSite_PermitAutoHide`, which
queries command 7 on the band site and answers no when the command is
supported and not enabled.

Inferred, not observed:

- Which flyout is which (thumbnail preview, hover card, other) among +0x240,
  +0x248 and +0x250.
- The route from the band site's command 7 to `PermitAutoHide` was not
  followed instruction by instruction.
- How often a button flashes is decided outside `taskbar.dll`.

## Cloaking

The primary's cloak is one call: `TrayUI::UpdateCloakState(animate)`
(0x102068). It cloaks when +0x1fb or +0x1fc is set, or when +0x1bc is 1, and
uncloaks otherwise, through `ImmersiveTray::SetCloakWithAnimation`
(0xffcb0), which sets `DWMWA_CLOAK`.

Callers: `_CloakUntilNextRender` (from `SetStuckPlace` and
`UpdateTaskbarSize`), `OnTimer` timer 0x1e, `OnGamingFullScreenChanged`
(0xfc0e0), `_HandleAssignedAccessTaskbarBlocking` (0x103068), and one
`WndProc` case that follows a successful `ImmersiveTray::RaiseWindow`.

`CSecondaryTray::v_WndProc` (0x33970) has the matching case and calls
`CSecondaryTray::_UpdateCloakState`.

`DWMWA_CLOAK` is one value per window. Windows and any other code in
`explorer.exe` write the same value. Each `UpdateCloakState` call overwrites
a cloak set by other code, and other code can uncloak a taskbar Windows has
cloaked.

Inferred, not observed:

- What a cloaked taskbar does to code outside `taskbar.dll`.
- The `WndProc` message that leads to the cloak case was not identified.

## The content child window

`TaskbarHost::SetAutoHide(flags)` (0xac840) sets `IsVisible` on the XAML
host child window to `(flags & 3) != 3`.

It is called from `TrayUI::_ReportAutoHideStateToChildControls` (0x5e87c),
from `CSecondaryTray::_UpdateChildControls_SetAutoHide`, and from
`TaskbarHost::Start_System`.

Order in Windows' own hide, for both kinds of taskbar: the slide first, then
this call. `SlideWindow` blocks until its animation ends, so the content is
told to disappear after the window has gone. When the slide is not animated
the call follows at once.

`TrayUI::Unhide` reports on every call, including the calls that change
nothing.

Inferred, not observed:

- The class name `Windows.UI.Composition.DesktopWindowContentBridge` does not
  appear in `taskbar.dll`. That the child window of that class is the one
  `IsVisible` acts on is an inference.
- When `IsVisible(false)` takes effect on screen. The call goes into another
  component.
- Whether Windows moves or re-lays-out the child while the taskbar is hidden.

## Z-order

`TrayUI::_ResetZorder(force)` (0x266c0) is the one place that sets the
primary's z-order. It picks a target:

- `HWND_TOPMOST` when the desktop is raised, when host slot 0x50 is true, or
  when host slot 0x48 is false. Slot 0x48 is read as "a full-screen (rude)
  window is on this monitor".
- With slot 0x48 true: top of the non-topmost windows when the taskbar is
  active, one of +0x1f8, +0x1f9, +0x1fa is set, Start is visible, a flyout is
  on this monitor, or the expandable taskbar is expanded.
- Otherwise `HWND_BOTTOM`.

It then compares the target with the window's `WS_EX_TOPMOST` style. If they
agree and `force` is 0 it does nothing. The helper at 0x93100 does the work:
`SetWindowPos(hwnd, target, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE |
SWP_NOACTIVATE | SWP_NOOWNERZORDER)`. For `HWND_TOPMOST` it checks the style
afterwards and, if the style is still missing, runs `_FixZorderEnumProc` over
all windows and tries again. Tooltip windows are then re-ordered with
`s_EnumTooltipWindowsProc`.

Callers: `Initialize`, `WndProc` (`WM_ACTIVATE`, 0x5c5, 0x5d1),
`RudenessChanged` (0x26b70), `OnLauncherVisibilityChange`,
`HandleHoverUIVisibilityChange` (0x26690, the only caller that passes
`force = 1`), `HandleJumpViewVisibilityChange`,
`HandleTaskViewVisibilityChange`, `HandleTrayFlyoutVisibilityChange`,
`OnStartMenuDismissed`, `ShyTaskbarExpansionChanged`.

`Unhide`, `_UnhideNow`, `_Hide` and `SlideWindow` do not call it. A reveal
does not touch the z-order.

`ImmersiveTray::RaiseWindow` (0x6610c) and `ImmersiveTray::HideWindow`
(0x6661c) talk to the immersive shell through COM objects. `RaiseWindow` can
send 0x5c5 to the taskbar.

Inferred, not observed:

- Host slot 0x48 as "rude window present". The reading comes from
  `RudenessChanged` calling `_ResetZorder` and from the telemetry name
  `ResetTrayZOrder`.
- A taskbar that keeps `WS_EX_TOPMOST` and still sits behind a window that is
  not topmost. `SetWindowPos(HWND_BOTTOM)` clears that style, so that state
  does not come from `_ResetZorder`. It may come from the immersive shell's
  window bands, which are outside `taskbar.dll`.
- In that state `_ResetZorder(0)` would do nothing, because it trusts the
  style. `_ResetZorder(1)` would call `SetWindowPos(HWND_TOPMOST)` again.
