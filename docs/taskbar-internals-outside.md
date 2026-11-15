# Taskbar internals outside taskbar.dll

Companion to `taskbar-internals.md`. Written on 2026-11-15.

Sources. All addresses are RVAs.

| Short name | File | Version | Symbols |
|---|---|---|---|
| `taskbar.dll` 9974 | WinSxS `..taskbar-dll_..10.0.26100.9974..` | 10.0.26100.9974 | public PDB |
| `taskbar.dll` 10066 | WinSxS `..taskbar-dll_..10.0.26100.10066..` | 10.0.26100.10066 | none published (HTTP 404) |
| `explorer.exe` | WinSxS `..explorer_..10.0.26100.9974..` | 10.0.26100.9974 | public PDB |
| `twinui.pcshell.dll` | WinSxS `..twinui-pcshell_..10.0.26100.9974..` | 10.0.26100.9974 | public PDB |
| `twinui.dll` | WinSxS `..twinui_..10.0.26100.9974..` | 10.0.26100.9974 | public PDB |
| `SettingsHandlers_DesktopTaskbar.dll` | WinSxS `..d..ar-settingshandlers_..10.0.26100.9818..` | 10.0.26100.9818 | public PDB |

The machine moved to build 10066 during this work. The 10066 copies of
`explorer.exe`, `twinui.pcshell.dll`, `twinui.dll`, `ShellHost.exe` and
`Taskbar.View.dll` (2610.27000) have no published PDB. Everything outside
`taskbar.dll` was therefore read in the 9974 files.

Method. Linear disassembly (capstone) with names from the public symbols.
The REA (Ghidra) session was lost twice to machine restarts and was not used
for the findings below; there is no pseudocode behind them, only
instructions.
"Observed" means the instructions were read. Function names in 10066 are the
9974 names of the structurally identical function.

Edge numbers are the `ABE_` values: 0 left, 1 top, 2 right, 3 bottom.

## Q0. Edge change and secondary unhide as window messages

### The private setting message

Observed, 9974:

- `TrayUI::_HandleTrayPrivateSettingMessage` (0x103584) has exactly one
  reference in the DLL: a direct call at 0x1a204 in `TrayUI::WndProc`
  (0x18c10). No vtable or other data holds its address.
- `WndProc` reaches that call for message `0x5CA` only (`sub eax, 0x5ca; je`
  at 0x18d61). It passes `wParam` (low 32 bits) as the setting id and
  `lParam` as the value. The handler's return value is the message result.
- `WndProc` constructs a `DeferWorkAreaChangesGuard` on the host before the
  call and destroys it after (0x1a1f5, 0x1a211). That is host slot 0x130
  `StartDeferringWorkAreaChanges` and slot 0x138
  `StopDeferringWorkAreaChanges`.

Observed, 10066:

- The handler is at 0x1008CC. It is instruction-for-instruction the same
  function: same setting switch (1 to 7), same vtable slots, same `+0x68`
  store for setting 4, same `SendMessage 0x46a` for setting 1.
- Its only reference is a direct call at 0xB247B in `TrayUI::WndProc`
  (0xB0F80), reached for message `0x5CA` only (`sub eax, 0x5ca; je` at
  0xB2209), with the same argument order and the same guard around it.

So in both builds: message `0x5CA`, `wParam` = setting id, `lParam` = value.
"Set stuck place to edge E" is `wParam == 6`, `lParam == E`.

### Who sends it

Every sender found uses `SendMessageW`. None posts it.

| Sender | Binary, function, RVA | Message |
|---|---|---|
| Settings app and settings data model | `SettingsHandlers_DesktopTaskbar.dll`, `DesktopTaskbarSettingsSingleton::Location(LocationMode)` 0x37B90, called by `DesktopTaskbarLoSetting::SetValue` 0x1DF8C | writes `TaskbarLocation` under `HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\Advanced`, then `0x5CA, 6, edge` |
| Registry watcher | same DLL, `ExplorerAdvancedKeyChanged` 0x3435C | when the `Advanced` key changes and `UpdateLocationFromRegistry` (0x3B848) reports a new value: `0x5CA, 6, edge` |
| Policy | same DLL, `TaskbarPositionPolicyChanged` 0x3A590 | `0x5CA, 6, edge` |
| Auto-hide switch in Settings | same DLL, `DesktopTaskbarAutohideSetting::SetValue` 0x2197C and `GetValue` 0x2186C | `0x5CA, 4, on` and `0x5CA, 3, 0` |
| Other settings (search, task view) | same DLL, `SendTaskbarSettingChangedWindowMessage` 0x3962C | `0x5CA, id, value` |
| WinRT `TaskbarSettings` in `taskbar.dll` | `TaskbarSettings::Location` setter 0x13FF68 | with `Feature_59213768` on it sets the `SystemSettings.DataModel` item (which ends in the first row). With the feature off it sends `0x5CA, 6, edge` itself |
| WinRT `TaskbarSettings` in `taskbar.dll` | `get_HideTaskbarInDesktopMode` 0x141110, `put_HideTaskbarInDesktopMode` 0x141950, `put_HideTaskbarInTabletMode` 0x1419D0 | settings 3, 4 and 10 |
| Taskbar context menu | `explorer.exe`, `CTray::Command` 0xDC560 (0xDCB44, 0xDCBA6) | settings 10 and 1 |

The settings handler DLL finds the window in `EnsureTrayWindow` (0x341A4).
It runs in whichever process loads the handler (the Settings app, or
Explorer through the data model). In both cases the message arrives through
`SendMessageW` and is handled synchronously in `TrayUI::WndProc`.

### Edge changes that do not use message 0x5CA

- `TrayUI::ApplyRemoteSettings` (0xF3BA0, slot 0xA8 of the `ITrayUI`
  vtable). Its caller is `CTray::_ApplyRemoteSettings` (`explorer.exe`
  0xFA890), called only from `CTray::v_WndProc` (0x5A50) for message `0x5B2`
  (jump table index 0x61 from base 0x551). `wParam` is the packed value: low
  byte is the edge, bit 19 is auto-hide. This also arrives at the tray
  window, so a subclass sees it. After the call `CTray` broadcasts
  `WM_SETTINGCHANGE` "TraySettings" and starts timer 0x10 at 500 ms.
- `TrayUI::SetStuckPlace` (0x100220) is referenced only through slot 0x100
  of the `ITrayComponentHost` vtable. The two callers found are the setting
  6 handler and `ApplyRemoteSettings`.

### Equivalence with the hook

Intercepting `0x5CA` with `wParam == 6` in the subclass, doing the "before"
work, calling the next window procedure and then doing the "after" work
brackets the same code as a hook on the handler, plus the
`DeferWorkAreaChangesGuard`. Its destructor makes the host apply deferred
work-area changes, so with the subclass the "after" work runs after that
flush. With the hook it ran before it.

The handler does nothing for setting 6 when `lParam` equals the current
`GetStuckPlace()` and `GetMultiMonTaskbarState()` is 0. The subclass can use
the same test to skip its bracket.

What stays asynchronous in both approaches: the posted `0x5C3` announcement,
the cloak timer `0x1E`, and the work of the consumers in Q2.

### Unhiding a secondary taskbar by message

`CSecondaryTray::v_WndProc` is 0x33970 in 9974 and 0x61780 in 10066. The
cases below are identical in both builds. `CSecondaryTray::_Unhide(flags,
request)` is 0xBAE8C in 9974 and 0x15BBCC in 10066. Flags bit 0: no
animation. Bit 1: do not start the hide timer again.

| Message | wParam, lParam | Effect |
|---|---|---|
| `WM_ACTIVATE` (6) | wParam not 0 | `_Unhide(0, 8)`: animated, hide timer started again. Then `ImmersiveTray::StopInputObservation` |
| `WM_ACTIVATE` (6) | wParam 0 | `_AutoHide(false)` if auto-hide is on: starts timer 2 at 500 ms |
| `0x4F6` | wParam not 0 | `_HandleTrayFlyoutVisibilityChange(true)` (0x161820; 10066 0x15B874): flyout count `+0x110` plus 1. If auto-hide is on and the count is above 0: `_Unhide(3, 4)`, then `KillTimer(2)` |
| `0x4F6` | wParam 0 | count minus 1. If auto-hide is on and the count is 0 or less: `_AutoHide(false)`, which starts timer 2 |
| `0x5BE` | wParam not 0, lParam 0 | `_HandleImmersiveInvoke(2)` (0x161700; 10066 0x15B764): `_Unhide(2, 1)`: animated, hide timer not started. Nothing else, except `UpdateTaskbarVisibility` under `Feature_55479964`. Result 1 |
| `0x5BE` | wParam not 0, lParam not 0 | `_Unhide(3, 1)`, then a helper at 0x15FD98 that has no name in the public symbols (the primary's twin at 0xECF8C calls `ImmersiveTray::RaiseWindow`, see Q4), then `ReplyMessage` and host slot 0x170 |
| `0x5BE` | wParam 0 | hides the tray through `ImmersiveTray::HideWindow` and resizes. Not an unhide |
| `0x5C5` | | `_ResetZorder(0)` |
| `0x5C2` | | `_ImmersiveResize(false)` |

So two message-level routes exist for "unhide and do not re-arm":

- `0x5BE` with wParam 1, lParam 0. Animated. One call, no counter.
- `0x4F6` with wParam 1. Not animated. It also raises the flyout count, so
  the tray stays up until a matching `0x4F6` with wParam 0 arrives. That
  second message starts the 500 ms hide timer. This is the hold Windows
  itself uses for tray flyouts.

The primary has the same two messages. `TrayUI::WndProc` maps `0x4F6` to
`HandleTrayFlyoutVisibilityChange(wParam != 0)` (0xFA71C): count `+0x110`,
`Unhide(3, 4)` and `KillTimer(2)` when above 0, `_SetAutoHideTimer` when 0,
then `_ResetZorder(0)`. It maps `0x5BE` to `_HandleImmersiveInvoke`
(0x1031AC) unless `+0x194` is 1.

Both secondary routes go through `_Unhide`, which does nothing unless flags
bit 1 (hidden) is set at `+0xD0`. With `Feature_41356296` on, `_Unhide` and
`_AutoHide` only call `TaskbarModel::NotifyShouldStayExpandedChanged` and
return (0xBAEB8, 0x161387).

Inferred, not observed:

- That no sender outside the files scanned posts `0x5CA`. The scan covered
  the settings handler DLLs, `explorer.exe`, `twinui.dll`,
  `twinui.pcshell.dll` and `taskbar.dll`. Third-party tools can send it too.
- Who sends `0x5B2`. Byte patterns for it exist in `twinui.dll` and
  `twinui.pcshell.dll` but the functions they fall in are unrelated; the
  sender was not found.
- That a `0x4F6` sent by the mod is safe when it is not balanced. The count
  is a plain integer; an extra "visible" keeps the taskbar up for the life
  of the process, and an extra "hidden" makes it negative.
- The state of `Feature_41356296` on a given machine.

## Q1. Auto-hide reads "off" after Explorer starts, then "on"

Observed:

- Host slot 0x20 is `CTray::IsTabletModeEnabled` (`explorer.exe` 0x6E450). It
  returns the byte at `CTray+0x1AA`. `TrayUI::_GetSaveStateAndInitRects`
  (`taskbar.dll` 0x5EE40) stores the saved bit at `+0x68` (0x5F06B), then
  copies it to `+0x6C`, or stores 0 there when this slot returns true
  (0x5F075 to 0x5F085). On a desktop this is not what clears the flags.
- `_GetSaveStateAndInitRects` has one caller, `_RestoreWindowPos` (0x60518),
  which has one caller, `_CreateWindows` (0x6004C). The registry is read once
  per Explorer start. Nothing reads it again later.
- `TrayUI::_KickStartAutohide` (0xD9A94) runs from `TrayUI::Initialize`
  (0x6AEC0, `ITrayUI` slot 0x68, called by `CTray::_SyncThreadProc`,
  `explorer.exe` 0x28020). When `GetAutoHideFlags() & 1` is 0 it calls
  `SetAutoHideFlags(0)`. `SetAutoHideFlags` (0x3CC90) writes `flags` to
  `+0x6C` and `flags & 1` to `+0x68` (0x3CCD4, 0x3CCDD). So after this call
  both the flags and the in-memory saved bit are 0. The value from the
  registry is gone from memory.
- Bit 0 of `+0x6C` can only come back through `SetAutoHideState(1)`
  (0xA6720). All routes to it, and where the value comes from:

| Route | Value comes from | Can it turn auto-hide on after the bit was zeroed |
|---|---|---|
| `0x5CA`, setting 4 (`_UpdateAutoHideState`, 0x103F68) | `lParam` of the message | yes |
| `0x5B2` to `Shell_TrayWnd`: `CTray::_ApplyRemoteSettings` (`explorer.exe` 0xFA890) then `TrayUI::ApplyRemoteSettings` (0xF3BA0) | bit 19 of `wParam` | yes |
| `WM_COPYDATA` appbar message 0xA (`ABM_SETSTATE`): `CTray::_OnAppBarMessage` (`explorer.exe` 0x72A8, call at 0x7432) | bit 0 of the caller's `lParam` | yes |
| `0x5CA`, setting 6 | only re-applies when flags were 3 | no |
| `TrayUI::SetTaskbarModel` (0x100520, at 0x100634) | `+0x68` | no, it is 0 |
| `_HandleSettingChange` (0x67A18, at 0x67ACD) on `WM_SETTINGCHANGE` | `+0x68` | no, it is 0 |

- `explorer.exe` calls `ITrayUI` slot 0x70 (`SetAutoHideState`) from exactly
  one place, the `ABM_SETSTATE` case above. This was checked by listing every
  use of the `ITrayUI` pointer at `CTray+0x2690`.
- `SetTaskbarModel` stores the model, subscribes one delegate on the model's
  settings object whose body only calls `TrayUI::InvalidateWorkAreaAsync`
  (0x106940), and calls `SetAutoHideState(+0x68 & 1)`. It does not read a
  setting from anywhere else.
- `SettingsHandlers_DesktopTaskbar.dll` has no auto-hide value of its own.
  `DesktopTaskbarAutohideSetting::GetValue` (0x2186C) asks the taskbar with
  `0x5CA, 3`, and `SetValue` (0x2197C) sends `0x5CA, 4, value`.

So the three routes that can turn it on again all arrive as window messages
at `Shell_TrayWnd`, and each carries the value in the message. The mod's
subclass sees all three.

What this means for the mod:

- The user's setting at the moment Explorer starts is the registry value
  that `_GetSaveStateAndInitRects` loads through
  `TrayStuckRectData::LoadSettings` (0x5FC20): `StuckRects3`, the byte the
  mod already knows as 02 or 03. It is in `+0x68` from `_CreateWindows` until
  `_KickStartAutohide` runs.
- A mod that is loaded before `TrayUI::Initialize` can keep the value by
  making `GetAutoHideFlags` answer truthfully during `_KickStartAutohide`.
- A mod that is loaded later cannot recover it from `TrayUI`. It can read
  `StuckRects3` itself, unless `SaveTrayStuff` (0x9CEB0, `ITrayUI` slot
  0x158, called from `CTray::_SaveTray`) has already written the zeroed bit.
- To find out which of the three messages arrives at about one minute, log
  in the subclass: `0x5CA` with wParam 4, `0x5B2`, and `WM_COPYDATA`.

Inferred, not observed:

- Which of the three routes produces the late "on", and who sends it. No
  sender of `0x5B2` was found in any System32 binary by byte pattern. The
  name `ApplyRemoteSettings` suggests settings sync; that is a guess.
- That the mod's hook is not yet in place when `_KickStartAutohide` runs.
  If it is in place, `_KickStartAutohide` takes its other branch: flags 3,
  `Unhide(0, 0xB)`, then host slot 0x78 `CTray::AppBarSetAutoHideBar`
  (`explorer.exe` 0x4D5B0), and `SetAutoHideState(0)` if that registration
  fails. That would also end with both values 0. Which branch ran on the
  owner's machine was not observed.
- Whether `SaveTrayStuff` runs during the "off" minute.

## Q3. What counts as "a flyout is open"

Windows has no single answer. Five separate channels keep the taskbar up.
All were read in the 9974 files.

### Channel 1: message 0x5D1, Search only

- The only sender found is `PostTaskbarMessage(wParam, lParam)`
  (`twinui.pcshell.dll` 0x230414). It finds `Shell_TrayWnd` with
  `FindWindowW` and calls `PostMessageW(hwnd, 0x5D1, wParam, lParam)`.
- Its callers are `XamlLauncherState::PostCortanaOrSearchVisibility(bool,
  HMONITOR)` (0x23038C) and `XamlLauncherState::OnCortanaShown` (0x140780).
- So `0x5D1` reports the Search window: wParam visible, lParam monitor. It
  goes to the primary only. Quick Settings does not use it. Nothing else
  does.

### Channel 2: message 0x4F6, tray flyouts

- `CBaseTrayFlyoutExperienceManager::NotifyTrayFlyoutIsVisible(bool)`
  (`twinui.pcshell.dll` 0x266640) posts `0x4F6` with wParam visible to
  `TaskbarHelpers::GetTrayWindowForMonitor(monitor of the flyout rect)`
  (0x2F9A68). It is called from that class's `OnViewShown` (0x4025B0),
  `OnViewHidden` (0x402310), `OnViewDestroyed` (0x402110) and destructor.
- The same message is posted by
  `CPeopleBarFlyoutExperienceManager::NotifyTrayOfFlyoutVisibility`
  (0x3BDE2C), `CortanaStoreAppExperienceManager::NotifyTrayOfCortanaVisibility`
  (0x3DEBD8), `VoiceAssistantPositioner::NotifyTrayOfVisibility` (0x566CCC)
  and `MeetAndChatManager::ReportFlyoutVisibility` (0x6541A0).
- Byte patterns for the same send exist in `twinui.dll`, `shell32.dll`,
  `SndVolSSO.dll` and `Taskbar.View.dll`. Those call sites were not read.
- In `taskbar.dll` the message raises or lowers the count at `+0x110`
  (primary `HandleTrayFlyoutVisibilityChange` 0xFA71C, secondary
  `_HandleTrayFlyoutVisibilityChange` 0x161820). `_IsActive` reads the count.
  See Q0 for the exact effect.

### Channel 3: ShellViewCoordinator, the general visibility registry

`WindowsUdk.UI.Shell.ShellViewCoordinator` is a WinRT class. A component
creates one for a `ShellView` number and calls `ReportVisibility`. Any other
component creates one for the same number and reads `Visibility` or
subscribes to `VisibilityChanged`. `ViewVisibility` 0 is visible, 1 is
hidden (read in `CControlCenterExperienceManager::OnVisibilityChanged`,
0x21CD90, which reports `visible xor 1`).

Reporters in `twinui.pcshell.dll`:

| Reporter | Function, RVA |
|---|---|
| Quick Settings ("ControlCenter") | `CControlCenterExperienceManager::OnVisibilityChanged` 0x21CD90 |
| Notification center and calendar | `CActionCenterExperienceManager::OnVisibilityChanged` 0x3A4A48, per monitor and global |
| Start | `XamlLauncher::PublishLauncherVisibilityState` 0x2B20FC, per monitor |
| Search | `XamlLauncher::OnSearchAppVisibilityChanged` 0x492A90 |
| Jump list | `CJumpViewExperienceManager::Show` 0x3CAF40, `DismissImpl` 0x248DE4 |
| Network flyout | `CNetworkExperienceManager::OnViewShown` 0x3FCA30, `OnViewHidden` 0x3FC960 |
| Pen menu, touch keyboard, virtual touchpad, Meet and Chat, task view | 0x41E9C0, 0x24A718, 0x3D48E0, 0x6541A0, 0x5004E0 |

Readers in `taskbar.dll`:

- `TrayUI::_Initialize` (0x6B0A0) creates a report-only coordinator for
  `ShellView` 0x15 and keeps it at `TrayUI+0x400`. Its `VisibilityChanged`
  handler calls `TrayUI::OnNotificationAreaOverflowVisibilityChanged`
  (0xD78F4). So view 0x15 is the tray overflow ("hidden icons") flyout.
- `TrayUI::_ShouldAutoHideTray` (0x4BA7C, at 0x4BC69) reads `Visibility()`
  of that coordinator. When it is 0 (visible) the answer is "do not hide",
  telemetry reason 0xC. This is check 8 in `taskbar-internals.md`; that
  document describes it as "slot 0x78 returns 0".
- `CTaskBand::SubscribeForControlCenterVisibilityChange` (0xC794C)
  subscribes to `ShellView` 6. Its handler
  `CTaskBand::HandleControlCenterVisibilityChange` (0x123F98) stores
  `Visibility() == 0` at `CTaskBand+0x390` and calls
  `ImmersiveTray::HandleImmersiveModeChange` (0x6527C) with
  `CTaskBand::_GetModeChangeFlags` (0x65020), in which `+0x390` is flag
  0x20.

### Channel 4: immersive mode change, used by Quick Settings and Start

`ImmersiveTray::HandleImmersiveModeChange(host, hwnd, edge, flags)`
(0x6527C):

- If any of flags 0x79 is set (0x01 `CTaskBand+0x1E0`, 0x08 `+0x1E1`, 0x10
  `+0x1E2`, 0x20 Quick Settings visible, 0x40 expandable taskbar raised):
  `ImmersiveTray::RaiseWindow(host, hwnd, 0, 1)`.
- Else, if the pointer is over the taskbar window or its children, or
  inside its window rect: `RaiseWindow(..., 5)`.
- Else: `HideWindow` or `RaiseWindow(..., 2)` (lower), by flag 2 (desktop
  visible) and flag 4.

`RaiseWindow` (0x6610C) is the band change described in Q4. So while Quick
Settings is visible Windows does not use a counter or a monitor member. It
keeps the taskbar raised into a higher window band, and the taskbar's hide
timer is governed by the other checks. Callers of
`HandleImmersiveModeChange`: `HandleControlCenterVisibilityChange`,
`HandleImmersiveLauncherVisibilityChange` (0x654E0, 0x669F0),
`HandleSearchAppVisibilityChange` (0x64F10, 0x64DC0),
`HandleTaskViewVisibilityChange` (0xAEED0, 0x1605A0),
`HandleActiveZBandChanged` (0x643D0), `HandleRaiseExpandableTaskbarChanged`
(0xB0440), `SwitchToDesktopOnMonitor` (0x65F88).

### Channel 5: inside taskbar.dll

Already in `taskbar-internals.md`: Start through `GetLauncherVisibleMonitor`
(`+0x210`), jump lists and task-list flyouts through `PermitAutoHide`, the
foreground window test in `_IsActive`, and message 0x439 to the window at
`+0x80`.

### Does Quick Settings stop the hide timer

Observed: Quick Settings does not post `0x5D1` or `0x4F6`, and
`_ShouldAutoHideTray` does not read `CTaskBand+0x390` or `ShellView` 6.
The only checks that can hold the taskbar while Quick Settings is open are
`_IsActive` (foreground window owned by the taskbar) and the pointer test.
The Quick Settings window belongs to another process. So in the code read,
nothing stops timer 2 from hiding the taskbar while Quick Settings is open
and the pointer is elsewhere.

### What the mod can use

- For its own holds: `0x4F6` (count based, both taskbar kinds) and `0x5BE`
  (see Q0).
- For reading: `ShellViewCoordinator`. Interface ids as stored in
  `taskbar.dll` 9974: `IShellViewCoordinator`
  `bc6fd3a4-3561-5629-abdd-170ab289ef2f` (0x1DB350),
  `IShellViewCoordinatorFactory` `bb8446e1-05d0-5510-80dd-96e9ebd9e006`
  (0x1DCDE8), `IShellViewCoordinatorStatics`
  `a80c3df0-d5aa-5f92-ae7f-a69fd31a1b11` (0x1FE558). `ShellView` 6 is Quick
  Settings and 0x15 is the tray overflow. `IShellViewCoordinator3` has
  `GetVisibilityForMonitor`. This is an undocumented API.
- No single message or call returns "some flyout is open on monitor M".

Inferred, not observed:

- `ShellView` numbers other than 6 and 0x15. Constants seen next to
  coordinator constructors in `twinui.pcshell.dll`: 1 (search, in
  `CopilotHotkeyManager::InvokeSearch`), 2, 3 and 7 (task view manager), 0x12
  (jump view), 0x19, 0x1E, 0x1F, 0x20. The value used by the notification
  center was not read.
- Which classes derive from `CBaseTrayFlyoutExperienceManager`. Its only
  constructor call is a generic `MakeAndInitialize` (0x26A760). The clock,
  battery and volume flyouts are probable users; that was not read.
- The meaning of mode flags 0x01, 0x08 and 0x10.
- That Widgets reports through `ShellViewCoordinator`. Not read.
- The behaviour of the hide timer with Quick Settings open was derived from
  the checks, not watched.

## Q4. Z-order and window bands

Observed:

- The taskbar has two independent z-order states.
  1. Its window band. `ImmersiveTray::IsWindowRaised(hwnd)` (`taskbar.dll`
     0x66430) is `GetWindowBand(hwnd) != 1`. Band 1 is `ZBID_DESKTOP`.
  2. Inside band 1, its topmost state, set only by `_ResetZorder`
     (`taskbar-internals.md`).
- The band is changed in one place. `ImmersiveTray::RaiseWindow(host, hwnd,
  insertAfter, flags)` (0x6610C) compares `IsWindowRaised` with flag bit 0
  and returns `E_INVALIDARG` when they already agree. Otherwise it gets
  `ITrustedComponentForegroundControl` (through
  `SID_IImmersiveMonitorService`, `SID_EdgeUi` and
  `EnsureTrayForegroundControl`) and calls its slot 0x28. Then, for a tray
  window, it sends `0x5C5`, which is `_ResetZorder(0)`.
- Slot 0x28 is `CTrustedComponentForegroundControl::RaiseTaskbarWindow(hwnd,
  insertAfter, raise)` (`twinui.pcshell.dll` 0x1DDC70). It calls
  `CPrivilegedPresentationOperations::SetWindowBand(hwnd, insertAfter,
  raise ? 6 : 1)` (0x1DD3C0), which calls `SetWindowBand`. Band 6 is
  `ZBID_IMMERSIVE_MOGO`. A window in band 6 is above every window in band 1,
  topmost or not.
- `explorer.exe` and `taskbar.dll` do not import `SetWindowBand`. Only
  `twinui.pcshell.dll` does.
- When Windows raises the taskbar into band 6: see Q3 channel 4. Also
  message `0x5BE` with lParam not 0: `TrayUI::_HandleImmersiveInvoke`
  (0x1031AC) calls `Unhide(3, 1)` and then a helper at 0xECF8C that calls
  `IsWindowRaised` and `RaiseWindow`.
- Who sends `0x5BE`: `CTaskbarInvoker::v_Invoke(bool, POINT, RAW_INPUT_TYPE)`
  (`twinui.dll` 0x1D92C0) with `SendMessageW(tray, 0x5BE, invoke, flag)`.
  With `Feature_55479964` on it posts `0x5DB` with the same parameters. This
  is the edge invoker: the code that brings the taskbar up when the pointer
  reaches the screen edge over a full-screen window. `taskbar.dll` itself
  sends `0x5BE` from `HandleImmersiveModeChange` (0x6538A) and
  `_SetStuckMonitor` (0x5F511).
- Rude (full-screen) windows are found by `CGlobalRudeWindowManager` in
  `twinui.dll` (`RecalculateRudeWindowState` 0x11CE0). The rect it tests
  comes from `RudeWindowWin32Functions::GetWindowRectForFullscreenCheck`
  (0x13C10): the client rect in screen coordinates when the window has
  `WS_CAPTION` or `WS_THICKFRAME` (mask 0xC40000), else the window rect.
- The result reaches the taskbar through
  `CTray::OnRudeWindowStateChange` (`explorer.exe` 0x89550).
  Host slot 0x48 `CTray::IsStuckRudeApp` (0x73370) returns
  `CTray+0x90 != 0`. `TrayUI::RudenessChanged` (`ITrayUI` slot 0x48,
  0x26B70) then runs `_ResetZorder`.

Consequences:

- A window that draws its own title bar keeps `WS_THICKFRAME` and has a
  client rect equal to its window rect. When it is maximized on a display
  whose work area is the whole display, its client rect can cover the
  display. The test above then treats it as full screen. `_ResetZorder`
  puts the taskbar at `HWND_BOTTOM` unless the taskbar is active, Start or a
  flyout is on that monitor, or one of `+0x1F8` to `+0x1FA` is set.
- A reveal (`Unhide`, `_UnhideNow`, `SlideWindow`, or a window move by the
  mod) does not call `_ResetZorder` and does not change the band. A taskbar
  revealed by the pointer over such a window stays where `_ResetZorder` left
  it.
- Windows' own answer for that case is the edge invoker: `0x5BE` with
  wParam 1 and lParam 1, which unhides and raises the taskbar into band 6.
  The matching lowering is `0x5BE` with wParam 0, or the next
  `HandleImmersiveModeChange`.

The supported ways to restore the taskbar's place, from the code:

1. `0x5C5` to the tray window: `_ResetZorder(0)` and `_SetAutoHideTimer`.
   It follows Windows' rules, so it leaves the taskbar at the bottom while
   a rude window is on the monitor and the taskbar is not active.
2. `0x5BE` with wParam 1, lParam 1: Windows' "show over a full-screen
   window". It changes the band.
3. Activating the taskbar. `WM_ACTIVATE` runs `_ResetZorder`, and an active
   taskbar is placed at the top of the non-topmost windows.

Inferred, not observed:

- That the owner's symptom is the rude-window case. The symptom as reported
  ("keeps `WS_EX_TOPMOST` and is behind a normal window") does not match
  it exactly: `SetWindowPos(HWND_BOTTOM)` clears `WS_EX_TOPMOST`. No code
  was found that leaves a band 1 topmost window behind a band 1 normal
  window. To tell the cases apart, log at the moment of the symptom:
  `GetWindowBand` of the taskbar and of the covering window, the taskbar's
  `WS_EX_TOPMOST` bit, and whether a `0x5C5` changes anything.
- That applications avoid the full-screen classification on the bottom edge
  by leaving one pixel free on the edge where an auto-hide appbar is
  registered (`ABM_GETAUTOHIDEBAREX`). This is known application behaviour,
  not read here. If it is right, the registration made by host slot 0x78
  `CTray::AppBarSetAutoHideBar` (`explorer.exe` 0x4D5B0) for the correct
  edge and monitor matters to the symptom.
- How `RecalculateRudeWindowState` compares the rect with the monitor. Only
  the rect source was read.
- The names of bands 1 and 6. The numbers are in the code; the names are
  from public descriptions of `ZBID`.

## Q2. The consumer of the edge announcement

Observed:

- `TrayUI::WndProc` on `0x5C3` calls `ImmersiveTray::NotifyTrayStuckPlaceChanged(host,
  monitor = lParam, edge = wParam)` (0xA5A84), only when host slot 0x88
  `CTray::GetImmersiveShell` is not null. The helper (0x9E94C) maps the edge
  through a table that is the identity (0, 1, 2, 3) and queues a task.
- The task (0x188298) does two calls.
  1. `SID_IImmersiveMonitorService`, then the monitor's `SID_EdgeUi`
     service, interface `6e6c3c52-5a5e-4b4b-a0f8-7fe12621a93e`, slot 0x48:
     `CEdgeUiManager::NotifyTrayStuckPlaceChanged(place)` (`twinui.dll`
     0xC1D40). It returns at once when the value at `+0x1E8` is already
     `place`. Otherwise it stores it and calls `_LayoutEdgeUiInputs(9)`.
  2. `SID_ImmersiveLauncher`, interface
     `bcf1f74f-2413-46b3-8707-ec2fca11965d`, slot 0x18:
     `XamlLauncher::TrayEdgeMayHaveChanged(place, monitor)`
     (`twinui.pcshell.dll` 0x239210), which calls
     `ImmersiveMonitorHelper::TrayEdgeMayHaveChanged` (0x239254). It stores
     nothing. If the monitor is the launcher's monitor and a state call
     (slot 0x68) returns true, it calls slot 0xA0 on the same object.
- So the edge is stored in one place outside `taskbar.dll`: one value per
  monitor in `CEdgeUiManager` (`+0x1E8` from the interface pointer).
  `CEdgeUiManager::GetTrayStuckPlace` (`twinui.dll` 0x277B0) returns it.
- Who reads it, and when:

| Surface | Function, RVA (`twinui.pcshell.dll`) | Source of the edge | When |
|---|---|---|---|
| Tray flyouts | `CBaseTrayFlyoutExperienceManager::GetAppBarPositionForMonitor` 0x400590 | `SID_EdgeUi` slot 0x50, then slot 0xA8 | each time it positions |
| Start | `CStartExperienceManager::GetMonitorInformation` 0x7A478 | with `Feature_59213768`: `WindowsUdk.UI.Shell.TaskbarLayout.Current().GetLocation(monitor)`. Otherwise `SID_EdgeUi`. It also reads the `IsAutoHideEnabled` window property of the taskbar | each time it positions |
| Search | `SearchAppDesktopExperienceView::GetStuckPlace` 0x4078BC, `ExtractMonitorInfo` (reads `IsAutoHideEnabled`, 0x809ED) | not read further | |
| Jump list | `CJumpViewExperienceManager::GetMonitorInformation` 0x3C9AD4 | not read further | |
| Quick Settings | `CControlCenterExperienceManager::PositionView` 0x6D5A4 | no edge value. It uses `IDisplayMonitorInfo.StaticSafeWorkArea` of the monitor under a point | each time it positions |

- `StaticSafeWorkArea` is published by `taskbar.dll`. `TrayUI` is the server
  of `WindowsUdk.UI.Shell.DisplayMonitorInfoCollection`.
  `TrayUI::GetDisplayMonitorInfoStructForMonitor` (0x3D8E0) builds the
  record from `GetMonitorInfo`, `GetStuckRectForMonitor` (0x3DDF0) and
  `GetVisualRect` (0x3C670), with `SubtractRect`. The record is pushed to
  the collection (`AddOrUpdateMonitor`) only from:
  - `UpdateDisplayMonitorInfoCollectionWorkAreas` (0x67320), called from
    `_OnSettingChange` when wParam is `SPI_SETWORKAREA` (0x67D98),
    from `_OnThemeChanged`, and from the destructor of
    `SuppressDisplayMonitorInfoCollectionUpdates` (0xDEDC0);
  - `AutoHideChangedOnMonitor` (0xB4B7C), which is the tail of
    `SlideWindow`;
  - `ShyTaskbarExpansionChanged` (0xB4C00).
- The `IsAutoHideEnabled` window property has four readers, all in
  `twinui.pcshell.dll`: Start (0x7A8EF), Search (0x809ED),
  `CTaskContinuityActivitiesExperienceManager` (0x3B7B11) and
  `MeetAndChatManager::CalculateMACFlyoutPosition` (0x651AD2).

Why the panels were placed for the previous edge:

- The panels read published state each time they open. They do not sample
  at notify time. The published state has two parts that are refreshed by
  different events: the edge in `CEdgeUiManager` (refreshed by `0x5C3`) and
  the per-monitor record with the static safe work area (refreshed by a
  work-area setting change or by the tail of `SlideWindow`).
- In Windows' own edge change on an auto-hide taskbar,
  `SetAutoHideState(0)` runs `_UnhideNow`, which runs `SlideWindow`, whose
  tail pushes the record. Then `_UpdateAutoHideState(1)` hides again through
  `SlideWindow`. With auto-hide off, the work area changes and the
  `SPI_SETWORKAREA` broadcast pushes the record.
- With the mod active on a non-bottom edge, the work area is the whole
  display before and after the move, so no broadcast follows. If the mod
  also replaces or suppresses the slide, nothing pushes the record, and
  Quick Settings keeps the record made for the previous edge. Making the mod
  transparent during setting 6 lets Windows' own slides run, which push the
  record. This fits the fix that worked.

Inferred, not observed:

- The paragraph above is a reading of which events push the record. The
  stale record itself was not observed.
- It does not explain the owner's experiment in which a hand-made
  `WM_SETTINGCHANGE` with `SPI_SETWORKAREA` did not repair the placement.
  Possible reasons, none checked: the suppress guard was active, the stuck
  rect for the monitor was itself not updated, or the panel measured takes
  its edge from `CEdgeUiManager` and not from the record.
- Which panel the measured window was. The notification center's
  positioning function was not read.
- Where `TaskbarLayout.GetLocation` gets its value.

## Q5. Loose ends

(a) Host (`ITrayUIHost`, `CTray` in `explorer.exe`, vtable 0x1F1E80) slots.
Observed from the vtable and the public symbols:

| Slot | Method | RVA |
|---|---|---|
| 0x18 | `GetWindow` | 0x3EEE0 |
| 0x20 | `IsTabletModeEnabled` | 0x6E450 |
| 0x28 | `IsLauncherVisible` | 0x784A0 |
| 0x30 | `IsDestroyed` | 0x6DF10 |
| 0x38 | `IsModeImmersive` | 0x89A80 |
| 0x48 | `IsStuckRudeApp` | 0x73370 |
| 0x50 | `IsProcessingDesktopRaise` | 0x6F800 |
| 0x78 | `AppBarSetAutoHideBar(hwnd, register, edge, monitor)` | 0x4D5B0 |
| 0x88 | `GetImmersiveShell` | 0x7B400 |
| 0xB8 | `StuckAppChange(hwnd, rect, rect, int)` | 0x715F0 |
| 0xC0 | `AppBarGetAutoHideBar(edge, monitor)` | 0x8E70 |
| 0xC8 | `AppBarActivationChange2(hwnd, edge)` | 0x88F0 |
| 0xD8 | `Command` | 0xDC560 |
| 0xF8 | `AppBarNotifyAll(monitor, message, hwnd, lParam)` | 0x72C70 |
| 0x130 | `StartDeferringWorkAreaChanges` | 0x4CB00 |
| 0x138 | `StopDeferringWorkAreaChanges` | 0x7470 |
| 0x140 | `WorkAreaMayHaveChanged(monitor, hwnd)` | 0x71710 |
| 0x280 | `IsGamingFullScreenActive` | 0xE64A0 |

The reading of slot 0x48 as "rude window present" in `taskbar-internals.md`
is confirmed by the name.

(b) Messages.

- `0x32A`: not determined. `TrayUI::WndProc` handles it only for wParam
  0x11 and lParam 1 to 4. Byte patterns for `mov edx, 0x32A` in
  `twinui.pcshell.dll`, `twinui.dll`, `shell32.dll` and `ExplorerFrame.dll`
  fall in functions that do not look related
  (`ShellPositionerManager::HandleActivationRequest`, `EdgeUiHint`). None was
  read.
- `0x5C4`: not determined for the tray. `CImmersiveMonitorManager::
  _HandleDisplayChangeNotificationInternal` (`twinui.dll` 0x6DF48, at
  0x6E0A6) posts `0x5C4` to the window kept at its own `+0xD8`. Whether that
  window is the tray was not read.
- `0x5C5` is sent by `ImmersiveTray::RaiseWindow` (0x663B2).
- `0x5BE` and `0x5DB`: see Q4.

(c) The window at `TrayUI+0x80`: not determined. No store to that member was
found by pattern in `TrayUI` functions. `TrayUI::_CreateWindows` (0x6004C)
creates `LegacySystrayCompatStubs` (0xEFB6C, kept at `+0x3F8`), which has
`CreateTrayNotifywnd` (0xD5A0C). The message numbers it receives (0x439,
0x465, 0x466, 0x46A, 0x46C, 0x46E) are in the range the notification area
used historically. That it is the legacy `TrayNotifyWnd` stub is a guess.

(d) The content child window.

- The class `Windows.Internal.Shell.XamlExplorerHost.XamlExplorerHostChildWindow`
  is implemented in `twinui.pcshell.dll` (constructor 0x4D3BC,
  `Initialize` 0x4DDC0). `Initialize` creates a
  `Windows.UI.Xaml.Hosting.DesktopWindowXamlSource`, attaches it to the
  parent through `IDesktopWindowXamlSourceNative2`, and calls `SetWindowPos`
  and `ShowWindow` once on the island window.
- The window class name `Windows.UI.Composition.DesktopWindowContentBridge`
  appears as a string in `Windows.UI.dll` and `SystemTray.dll`, not in
  `twinui.pcshell.dll` or `taskbar.dll`.
- The child's position is set through `put_Bounds` (0x1F8A20), which is one
  `MoveWindow`. In `taskbar.dll` the caller is `TaskbarHost::UpdatePosition`
  (0x3CAB0): it reads the parent's client rect, sets the bounds and sets the
  XAML root's width and height. Its callers are `TrayUI::SizeWindows`
  (0x3C060, two sites), `CSecondaryTray::_ResizeAndPositionChildControls`
  (0xAB104) and `TaskbarHost::Start_System` (0xB994C).
- No window procedure or subclass of the child that reacts to its own
  position was found in `XamlExplorerHostChildWindow`. So the host does not
  watch the child. It moves it back to the parent's client rect whenever
  `UpdatePosition` runs.

(e) `IXamlExplorerHostChildWindow::IsVisible(false)` is
`put_IsVisible` (`twinui.pcshell.dll` 0x1EFB10): `ShowWindow(hwnd, 0)`.
`IsVisible(true)` is `ShowWindow(hwnd, 8)` (`SW_SHOWNA`). No animation and no
delay in this function.

Inferred, not observed:

- That the island window created by `DesktopWindowXamlSource` is the window
  of class `DesktopWindowContentBridge` seen as the tray's child.
- What triggers `CSecondaryTray::_ResizeAndPositionChildControls`. By its
  `WINDOWPOS` parameter it runs on a position change of the secondary tray
  window; its caller was not read.
- That nothing else moves the child. Only the two classes named were read.
  The XAML island code in `Windows.UI.Xaml.dll` was not read.

## Q6. Edge change left to bottom that reverts to left (10066, mod d4dc21c)

Read in `taskbar.dll` 9974 with symbols. The 10066 functions were compared
instruction by instruction after address normalisation and are identical:
handler 0x1008CC, `SetStuckPlace` 0xFD210 (9974 0x100220),
`_HandleWindowPosChanging` 0x44D54 (9974 0x41CA4), `_HandleSize` 0xA6454
(9974 0xAA594). Offsets below are from base 0 (`+0x140` edge, `+0x14C`
moving edge, `+0xF0` in-size-move, `+0x6C` flags).

### 1. What can set the edge to a value other than the one requested

Observed. The complete list of writes to `+0x140` in `TrayUI`:

| Writer | Value | When |
|---|---|---|
| `SetStuckPlace` 0x10029F | the requested edge | only when it differs from `+0x140` (`cmp`, `je` at 0x100268) |
| `_HandleWindowPosChanging` 0x41E66 | `_RecalcStuckPos(rect)` | only while `+0xF0` is not 0. The rect is the one in the `WINDOWPOS`; with `SWP_NOMOVE` or `SWP_NOSIZE` the missing part comes from the current window rect |
| `_HandleSize` 0xAA621 | `_RecalcStuckPos(NULL)`, which uses `GetWindowRect` of the taskbar | on `WM_SIZE`, only while `+0xF0` is not 0 and `(GetAutoHideFlags() & 3) != 3` |
| `_DoneMoving` 0xD8E28, 0xD8F0D | the moving edge `+0x14C` | only when `+0x14C` is not -1. It is set by `_HandleMoving` on `WM_MOVING` |
| `_GetSaveStateAndInitRects` | from the registry | once, at window creation |

- `+0xF0` is written in one function only: `SetStuckPlace` sets it to 1
  before its `SetWindowPos(hwnd, 0, stuck rect of the new edge, 0x214)` and
  to 0 after (0x100303, 0x10036D). It also calls `SetInSizeMove` (0x100060),
  which forwards to host slot 0x1C0.
- `_RecalcStuckPos` (0x103A00) takes the centre of the rect, sets `+0x14C`
  to -1 and returns `_CalcDragPlace(centre)` (0x1024B8). That function picks
  the display under the point and the nearest edge of it by the diagonals.
  A rect on the left stuck rect gives edge 0.
- So the edge is recomputed from geometry twice inside `SetStuckPlace`:
  from the requested rect in `WM_WINDOWPOSCHANGING`, and from the window's
  real rect in `WM_SIZE`. If the window is on the left rect when that
  `WM_SIZE` runs, the edge becomes 0 again. Nothing outside
  `SetStuckPlace`'s `SetWindowPos` can do this.
- When the request equals the current edge: the handler returns early
  (only `NotifySettingsChanged`, slot 0x3E0) unless
  `GetMultiMonTaskbarState()` is not 0. If it continues, `SetStuckPlace`
  itself skips everything except `SendMessage(0x45D)` (0x100268 to
  0x10038E). No `SetWindowPos`, no `+0xF0`, no write to the edge.

### 2. Who sends the second message

Observed in `SettingsHandlers_DesktopTaskbar.dll` (9818):

- `DesktopTaskbarSettingsSingleton::Location(mode)` (0x37B90) stores the
  value in its own cache (`+0x3C8`, valid flag `+0x3CC`), writes
  `TaskbarLocation` and sends `0x5CA, 6, mode`.
- `ExplorerAdvancedKeyChanged` (0x3435C) runs on a change of the `Advanced`
  key. `UpdateLocationFromRegistry` (0x3B848) compares the registry value
  with that cache and reports a change only when they differ. Then it sends
  `0x5CA, 6, value` again.
- The cache is per singleton, so per process. The process that set the
  value does not send twice. Any other process that has this DLL loaded
  sees a difference and sends the second message.

So a second `0x5CA, 6, 3` about 200 ms later is normal and does not need the
mod.

### 3. What depends on the flags, and the difference between 3 and 1

Observed, for the first message with stored flags 3 and the mod passing
`GetAutoHideFlags` through:

1. On the left edge Windows' `GetAutoHideFlags` answers 0 (the
   `Feature_61970341` mask). The handler's test `(flags & 3) == 3` is false,
   so it does not call `SetAutoHideState(0)` first.
2. `SetStuckPlace(3)` stores edge 3. From this instruction on the mask no
   longer applies, and Windows reads the stored 3.
3. In `WM_WINDOWPOSCHANGING` the edge is recomputed from the requested
   bottom rect (3). `GetDockedRect` then returns `_ComputeHiddenRect` for
   the bottom edge, because the flags are 3, and the request is overwritten
   with that hidden rect (0x41DD6). The window is moved below the display,
   not onto the bottom stuck rect.
4. In `WM_SIZE`, `_HandleSize` skips its recompute because the flags are 3.
5. Back in the handler, `Unhide(0, 7)` acts only if `+0x55` is not 0. If it
   acts: flags 1, `_UnhideNow`, `SlideWindow` to the bottom stuck rect, hide
   timer started.

With stored flags 1 instead: step 3 docks the window on the bottom stuck
rect, step 4 recomputes the edge from the real window rect, step 5 changes
nothing. That is the path Windows takes for a shown auto-hide taskbar.

Nothing in the sequence forces `SWP_NOMOVE` in this case; the rects differ.
The auto-hide bar registration (host slot 0x78) is not moved to the new
edge on this path, because `SetAutoHideState` is not called.

### 4. The mod's own code in build d4dc21c

Observed in the source:

- `TrayWatchProc` on `0x5CA`/6: `ReleaseAllBars`, clear `display` and
  `held`, set `g_inEdgeChange`, `DefSubclassProc`, set the 2500 ms settle
  time, clear `g_inEdgeChange`.
- `EdgeChangeInProgress()` is true for both messages and for the posted
  `0x5C3`. In that state: `TrayUI_GetAutoHideFlags_Hook` returns the
  original value, `TrayUI_Unhide_Hook` and `TrayUI_SlideWindow_Hook` call
  the original, and `BarsHandled()` is false, so `BarPosChanging` is not
  called and the `WINDOWPOS` is left as Windows wrote it.
- `BarPosChanging` runs after `DefSubclassProc`, so even when active it
  cannot change the rect that `_HandleWindowPosChanging` uses for the
  recompute. It can change where the window ends up, which is what
  `_HandleSize` reads.
- `ReleaseBar` returns early when `rec->placed` is false. The first release
  clears it, so at the second message the mod does not move the window.

So in this build the hooks do not alter what Windows asks during either
message. What the mod contributes is the state it left: stored flags 3 on an
edge where Windows normally keeps them at 0 or 1.

### 5. Conclusion

- The code explains the first message completely, and shows that with
  flags 3 it takes a path Windows does not take by itself (item 3).
- The code does not explain the second message. With edge 3 at entry, no
  instruction in the handler or in what it calls can write the edge. For
  the edge to become 0 there, `SetStuckPlace`'s `SetWindowPos` must have run
  with the window on the left rect during `WM_SIZE` and flags other than 3,
  or the edge was no longer 3 when the second message arrived. Neither was
  observed.
- The HEAD change (call `Unhide(3, 7)` before forwarding) is supported by
  the code for what it is meant to do. Called while the mod's
  `GetAutoHideFlags` hook still answers the stored flags, `Unhide` sees 3,
  stores 1 and shows the taskbar without animation and without starting
  the hide timer. The edge change then runs as in the flags 1 case. It
  needs `+0x55` to be non-zero, which was not read.
- A second message with the same edge is normal. The bracket can skip its
  release work when `lParam` equals the current edge.

Inferred, not observed:

- That the hidden-rect path of item 3 is the cause of the revert. It is the
  only difference from Windows' own sequence that the code shows, so it is
  the most likely cause; confidence low to medium.
- What wrote edge 0. To settle it, log in the subclass during both
  messages and for 2500 ms after: every `WM_WINDOWPOSCHANGING` and
  `WM_SIZE` with the `WINDOWPOS`, the window rect, `+0x140`, `+0x14C`,
  `+0xF0` and `+0x6C`; and every `0x5CA`, `0x5B2` and `0x553`.
- The callers of `SetStuckPlace` through its vtable slot other than the
  handler and `ApplyRemoteSettings`. A third caller would be another way to
  an edge 0.
- The value of `GetMultiMonTaskbarState()` on the owner's machine.
- The meaning of `+0x55`.
