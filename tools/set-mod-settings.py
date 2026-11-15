#!/usr/bin/env python3
"""Changes settings of the mod installed in the owner's Windhawk, through the
elevated task a release uses (tools/install-mod.ps1 does the writing). The
mod's settings key is read-only for an ordinary account.

    tools/set-mod-settings.py hideDelayMs=800 hideFully=0

Values are whole numbers. Windhawk tells the running mod to read them again.
"""
import json
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import windhawk_install as wi  # noqa: E402


def main(argv):
    settings = {}
    for arg in argv[1:]:
        name, sep, value = arg.partition('=')
        if not sep or not value.lstrip('-').isdigit():
            sys.exit(f'{arg!r}: expected name=whole number')
        settings[name] = int(value)
    if not settings:
        sys.exit(__doc__)
    staging = wi.staging_dir()
    os.makedirs(staging, exist_ok=True)
    for name in ('result.json', 'request.json'):
        if os.path.exists(os.path.join(staging, name)):
            os.remove(os.path.join(staging, name))
    shutil.copyfile(os.path.join(wi.TOOLS, 'install-mod.ps1'), os.path.join(staging, 'install-mod.ps1'))
    with open(os.path.join(staging, 'request.json'), 'w') as f:
        json.dump({'settings': settings}, f)
    r = subprocess.run([os.environ.get('SCHTASKS_CMD', 'schtasks.exe'), '/run', '/tn', wi.TASK],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f'cannot start the {wi.TASK} task ({(r.stderr or r.stdout).strip()})')
    try:
        result = wi._wait_for_result(os.path.join(staging, 'result.json'))
    except wi.InstallError as e:
        sys.exit(str(e))
    if not isinstance(result, dict) or not result.get('ok'):
        sys.exit(f'not changed: {result.get("error") if isinstance(result, dict) else result}')
    print('set ' + ', '.join(f'{k}={v}' for k, v in settings.items()))


if __name__ == '__main__':
    main(sys.argv)
