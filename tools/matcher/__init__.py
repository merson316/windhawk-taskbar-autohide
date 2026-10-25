"""Finds the mod's hooked taskbar.dll functions in builds Microsoft has not
published symbols for, by matching them against a build that has a PDB."""

# Field name (as in the mod's KnownBuild struct, in its order) -> public symbol.
HOOKED = {
    'getStuckPlace': '?GetStuckPlace@TrayUI@@UEAAIXZ',
    'getAutoHideFlags': '?GetAutoHideFlags@TrayUI@@UEAAIXZ',
    'setAutoHideFlags': '?SetAutoHideFlags@TrayUI@@UEAAXI@Z',
    'getDockedRect': '?GetDockedRect@TrayUI@@UEAAIPEAUtagRECT@@H@Z',
    'unhide': '?Unhide@TrayUI@@UEAAXW4TrayUnhideFlags@TrayCommon@@W4UnhideRequest@3@@Z',
    'slideWindow': '?SlideWindow@TrayUI@@UEAAXPEAUHWND__@@PEBUtagRECT@@PEAUHMONITOR__@@_N3@Z',
    'permitAutoHide': '?PermitAutoHide@CTaskListWnd@@UEAAHXZ',
}
