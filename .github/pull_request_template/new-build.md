Windows installed taskbar.dll **{version}**, and Microsoft has not published its symbols. `tools/match-build.py` matched the mod's seven functions against **{reference}**:

{evidence}

This PR adds the build to `kKnownBuilds` and bumps the mod to **{mod_version}**.

## Before merging

- [ ] Compiled this PR's `src/taskbar-autohide-any-edge.wh.cpp` in Windhawk
- [ ] Auto-hide works on a side edge
- [ ] Windhawk's log shows "using the built-in addresses for taskbar.dll {version}"
- Tested on OS build (winver): `_____`

Merge with `tools/release.sh {mod_version} --pr <this PR's number>`, not the merge button, so every commit stays signed with the owner's key.
