#!/usr/bin/env python3
"""Installs the working copy of the mod in the owner's Windhawk, through the
same elevated task a release uses (tools/windhawk_install.py).

That task only installs a version newer than the installed one, so the staged
copy is given the installed version with its last number raised by one. The
file in the repo is not changed. A real release has a higher version still.

    tools/install-dev-build.py [--debug] [file.wh.cpp]

--debug installs the debug build: the same source with its detailed log on.

It does not enable the mod: that stays the owner's switch in Windhawk.
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import windhawk_install  # noqa: E402
from make_debug_build import debug_source  # noqa: E402

KEY = r'HKLM\SOFTWARE\Windhawk\Engine\Mods\local@taskbar-autohide-any-edge'


def installed_version():
    out = subprocess.run(['reg.exe', 'query', KEY, '/v', 'Version'], capture_output=True, text=True, cwd='/mnt/c').stdout
    m = re.search(r'Version\s+REG_SZ\s+(\d+)\.(\d+)\.(\d+)', out)
    if not m:
        sys.exit('cannot read the installed Version')
    return tuple(int(x) for x in m.groups())


def main():
    args = [a for a in sys.argv[1:] if a != '--debug']
    path = args[0] if args else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'taskbar-autohide-any-edge.wh.cpp')
    with open(path, newline='') as f:
        source = f.read()
    if '--debug' in sys.argv[1:]:
        source = debug_source(source)
    major, minor, patch = installed_version()
    version = f'{major}.{minor}.{patch + 1}'
    source, n = re.subn(r'(?m)^(// @version\s+)\S+', lambda m: m.group(1) + version, source, count=1)
    if n != 1:
        sys.exit('no // @version line in the source')
    try:
        windhawk_install.install(version, source)
    except windhawk_install.InstallError as e:
        sys.exit(f'not installed: {e}')


if __name__ == '__main__':
    main()
