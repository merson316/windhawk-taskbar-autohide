"""Finds the mod's hooked taskbar.dll functions in builds Microsoft has not
published symbols for, by matching them against a build that has a PDB."""

# Field name (as in the mod's KnownBuild struct, in its order) -> public symbol.
# HOOKED are the functions the mod cannot run without: a build is accepted only
# with every one of them matched.
HOOKED = {
    'getStuckPlace': '?GetStuckPlace@TrayUI@@UEAAIXZ',
    'getAutoHideFlags': '?GetAutoHideFlags@TrayUI@@UEAAIXZ',
    'setAutoHideFlags': '?SetAutoHideFlags@TrayUI@@UEAAXI@Z',
    'getDockedRect': '?GetDockedRect@TrayUI@@UEAAIPEAUtagRECT@@H@Z',
    'unhide': '?Unhide@TrayUI@@UEAAXW4TrayUnhideFlags@TrayCommon@@W4UnhideRequest@3@@Z',
    'slideWindow': '?SlideWindow@TrayUI@@UEAAXPEAUHWND__@@PEBUtagRECT@@PEAUHMONITOR__@@_N3@Z',
    'permitAutoHide': '?PermitAutoHide@CTaskListWnd@@UEAAHXZ',
}

# Functions the mod works without. One that cannot be matched to the standard
# of the others is recorded as absent (0 in the table) and the build is still
# accepted. They follow HOOKED in the KnownBuild struct.
OPTIONAL = {
    'secondaryTrayUnhide':
        '?_Unhide@CSecondaryTray@@AEAAXW4TrayUnhideFlags@TrayCommon@@W4UnhideRequest@3@@Z',
}

# Every column of a kKnownBuilds row after the PDB identity, in order.
FIELDS = {**HOOKED, **OPTIONAL}


def reference_rvas(found):
    """Field -> RVA for a reference build, from pdb.publics(pdb, FIELDS.values()).
    Raises LookupError if a required function is missing; an optional one that
    is missing is left out."""
    missing = [f for f, n in HOOKED.items() if n not in found]
    if missing:
        raise LookupError(f'PDB lacks {", ".join(missing)}')
    return {f: found[n] for f, n in FIELDS.items() if n in found}


def row_values(rvas):
    """A kKnownBuilds row's addresses as written in the table: the required
    ones, then the optional ones up to the last that is known. C++ zero-fills
    what a row leaves out, so 0 (absent) needs no column of its own, and a row
    without optional addresses reads as rows did before there were any."""
    values = [rvas[f] for f in HOOKED] + [rvas.get(f, 0) for f in OPTIONAL]
    while len(values) > len(HOOKED) and not values[-1]:
        values.pop()
    return ', '.join(f'0x{v:X}' for v in values)
