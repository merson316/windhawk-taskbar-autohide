"""Installs a released version in the owner's Windhawk, after autorelease
publishes it. This half runs unelevated in WSL: it checks that Windhawk is
laid out as expected, compiles the tagged source with Windhawk's own
compiler into a staging folder, and starts the elevated task
WindhawkTaskbarModInstall (tools/install-watch-task.ps1 registers it). That
task runs tools/install-mod.ps1, copied into the staging folder here, which
copies the DLL and source in and points the registry at the new DLL; Windhawk
picks up the changed LibraryFileName by itself. Its answer comes back in
staging/result.json.

Env overrides (tests): WINDHAWK_DIR, WINDHAWK_DATA, INSTALL_STAGING, REG_CMD,
SCHTASKS_CMD, INSTALL_WAIT (seconds, default 120), INSTALL_EVERY."""
import json
import ntpath
import os
import re
import shutil
import subprocess
import time

import watch_build as wb

TOOLS = os.path.dirname(os.path.abspath(__file__))
COMPILE = os.path.join(TOOLS, 'compile-mod.sh')
MOD = 'local@taskbar-autohide-any-edge'
KEY = rf'HKLM\SOFTWARE\Windhawk\Engine\Mods\{MOD}'
TASK = 'WindhawkTaskbarModInstall'
INI = {'AppDataPath': r'%ProgramData%\Windhawk', 'RegistryKey': r'HKLM\SOFTWARE\Windhawk'}


class InstallError(Exception):
    """The version was not installed; the message says why."""


def windhawk_dir():
    return os.environ.get('WINDHAWK_DIR', '/mnt/c/Program Files/Windhawk')


def data_dir():
    return os.environ.get('WINDHAWK_DATA', '/mnt/c/ProgramData/Windhawk')


def reg_query(key):
    """The values of one registry key (not its subkeys) as strings, from
    `reg.exe query`; {} if the key cannot be read."""
    try:
        r = subprocess.run([os.environ.get('REG_CMD', 'reg.exe'), 'query', key], capture_output=True)
    except OSError:
        return {}
    if r.returncode != 0:
        return {}
    values, header = {}, 0
    for line in r.stdout.decode(errors='replace').splitlines():
        if not line.strip():
            continue
        if not line.startswith('    '):   # a key's name: this key, then its subkeys
            header += 1
            if header > 1:
                break
            continue
        m = re.match(r'    (.+?)    (REG_\w+)(?:    (.*))?$', line.rstrip('\r'))
        if m:
            values[m[1]] = m[3] or ''
    return values


def _read_ini(path):
    with open(path, 'rb') as f:
        raw = f.read()
    text = raw.decode('utf-16') if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else raw.decode('utf-8-sig')
    values = {}
    for line in text.splitlines():
        if '=' in line and not line.lstrip().startswith((';', '[')):
            k, v = line.split('=', 1)
            values[k.strip().lower()] = v.strip()
    return values


def layout_problems(windhawk_dir, data_dir, reg):
    """What about this Windhawk differs from the layout the install expects;
    [] if nothing. reg(key) returns a registry key's values."""
    ini = os.path.join(windhawk_dir, 'windhawk.ini')
    try:
        values = _read_ini(ini)
    except (OSError, UnicodeDecodeError) as e:
        return [f'cannot read windhawk.ini in {windhawk_dir}: {e}']
    if values.get('portable') != '0':
        return ['Windhawk is in portable mode']
    problems = [f'windhawk.ini: {k} is {values.get(k.lower())}, expected {v}'
                for k, v in INI.items() if values.get(k.lower(), '').lower() != v.lower()]
    if problems:
        return problems
    source = os.path.join(data_dir, 'ModsSource', f'{MOD}.wh.cpp')
    if not os.path.isfile(source):
        problems.append(f'no mod source at {source}')
    mods = os.path.join(data_dir, 'Engine', 'Mods', '64')
    if not os.path.isdir(mods):
        problems.append(f'no folder {mods}')
    key = reg(KEY)
    if not key.get('Version') or not key.get('LibraryFileName'):
        problems.append(f'registry key {KEY} is missing or has no Version and LibraryFileName')
    elif os.path.isdir(mods):
        dll = ntpath.basename(key['LibraryFileName'])
        if not os.path.isfile(os.path.join(mods, dll)):
            problems.append(f'the installed DLL {dll} is not in {mods}')
    return problems


def staging_dir():
    """%LOCALAPPDATA%\\taskbar-autohide\\install, as a WSL path."""
    if os.environ.get('INSTALL_STAGING'):
        return os.environ['INSTALL_STAGING']
    # cwd on a Windows drive: cmd.exe warns about a UNC working directory.
    local = subprocess.run(['cmd.exe', '/c', 'echo %LOCALAPPDATA%'], capture_output=True, text=True,
                           cwd='/mnt/c').stdout.strip()
    if not local or '%' in local:
        raise InstallError('cannot find %LOCALAPPDATA%')
    path = subprocess.run(['wslpath', '-u', local], capture_output=True, text=True, check=True).stdout.strip()
    return os.path.join(path, 'taskbar-autohide', 'install')


def stage(version, staging):
    """Compile v{version}'s source, as origin tagged it, into staging, with
    install-mod.ps1 and the request for the elevated task."""
    os.makedirs(staging, exist_ok=True)
    for name in ('result.json', 'request.json', 'mod.dll', 'mod.wh.cpp'):
        if os.path.exists(os.path.join(staging, name)):
            os.remove(os.path.join(staging, name))
    # From origin, into FETCH_HEAD only: the checkout's tags are not touched,
    # and a stray local tag of the same name is not what gets installed.
    wb.git('-C', wb.REPO, 'fetch', '-q', '--no-tags', 'origin', f'refs/tags/v{version}')
    src = wb.git('-C', wb.REPO, 'show', f'FETCH_HEAD:{wb.SRC}').stdout
    with open(os.path.join(staging, 'mod.wh.cpp'), 'w', newline='') as f:
        f.write(src)
    subprocess.run([COMPILE, os.path.join(staging, 'mod.wh.cpp'), os.path.join(staging, 'mod.dll')],
                   check=True, capture_output=True, text=True)
    shutil.copyfile(os.path.join(TOOLS, 'install-mod.ps1'), os.path.join(staging, 'install-mod.ps1'))
    with open(os.path.join(staging, 'request.json'), 'w') as f:
        json.dump({'version': version}, f)


def _wait_for_result(path):
    deadline = time.monotonic() + float(os.environ.get('INSTALL_WAIT', 120))
    every = float(os.environ.get('INSTALL_EVERY', 1))
    while True:
        try:
            with open(path, encoding='utf-8-sig') as f:
                return json.load(f)
        except (OSError, ValueError):   # not there yet, or being written
            pass
        if time.monotonic() >= deadline:
            raise InstallError('timed out waiting for the install task')
        time.sleep(every)


def install(version):
    """Install version in the owner's Windhawk. Raises InstallError."""
    problems = layout_problems(windhawk_dir(), data_dir(), reg_query)
    if problems:
        raise InstallError('; '.join(problems))
    try:
        staging = staging_dir()
        stage(version, staging)
        r = subprocess.run([os.environ.get('SCHTASKS_CMD', 'schtasks.exe'), '/run', '/tn', TASK],
                           capture_output=True, text=True)
    except subprocess.CalledProcessError as e:
        lines = (e.stderr or '').strip().splitlines() or [f'{e.cmd[0]} exited {e.returncode}']
        raise InstallError(lines[-1]) from None
    except OSError as e:
        raise InstallError(str(e)) from None
    if r.returncode != 0:
        raise InstallError(f'cannot start the {TASK} task ({(r.stderr or r.stdout).strip()}); '
                           'run tools/install-watch-task.ps1 as admin')
    result = _wait_for_result(os.path.join(staging, 'result.json'))
    if not isinstance(result, dict) or not result.get('ok'):
        raise InstallError(str(result.get('error') if isinstance(result, dict) else result))
    print(f'installed v{version}: {result.get("dll")}')
