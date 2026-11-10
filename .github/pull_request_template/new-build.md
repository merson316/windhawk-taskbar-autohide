Windows installed taskbar.dll **{version}**, and Microsoft has not published its symbols. `tools/match-build.py` matched the mod's seven functions against **{reference}**:

{evidence}

This PR adds the build to `kKnownBuilds` and bumps the mod to **{mod_version}**.

## Release

This PR releases itself as **v{mod_version}** once both CI checks pass, and installs itself in your Windhawk.

To stop that, convert this PR to a draft. Closing it also works, but do not delete its branch, or the next run opens a new PR.

Optional checks by hand:

- [ ] Compiled this PR's `src/taskbar-autohide-any-edge.wh.cpp` in Windhawk
- [ ] Auto-hide works on a side edge
- [ ] Windhawk's log shows "using the built-in addresses for taskbar.dll {version}"
