"""After a release the watcher installs the new version in the owner's
Windhawk: it checks the layout, compiles the tagged source unelevated into a
staging folder, and starts the elevated task that copies it in. These tests
use a fake Windhawk tree, a fake registry and a fake task; nothing here
touches the real Windhawk, the registry or the task scheduler."""
import json
import os
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.dirname(__file__))
from test_autorelease import ar, git, world  # noqa: E402,F401  (fixtures)
from test_watch import msdl  # noqa: E402,F401  (fixture)

import windhawk_install as wi  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SRC = 'src/taskbar-autohide-any-edge.wh.cpp'
KEY = r'HKLM\SOFTWARE\Windhawk\Engine\Mods\local@taskbar-autohide-any-edge'
DLL = 'local@taskbar-autohide-any-edge_1.18.0_500841.dll'
INI = ('\r\n[Storage]\r\nPortable={portable}\r\nCompilerPath=Compiler\r\nEnginePath=Engine\\1.7.3\r\n'
       'UIPath=UI\r\nAppDataPath=%ProgramData%\\Windhawk\r\nRegistryKey=HKLM\\SOFTWARE\\Windhawk\r\n')
REG_OUT = (f'\r\n{KEY}\r\n    LoggingEnabled    REG_DWORD    0x1\r\n    LibraryFileName    REG_SZ    {DLL}\r\n'
           '    Disabled    REG_DWORD    0x0\r\n    Include    REG_SZ    explorer.exe\r\n'
           '    Exclude    REG_SZ    \r\n    Version    REG_SZ    1.18.0\r\n\r\n'
           f'{KEY}\\Settings\r\n')


def make_windhawk(root, portable=0):
    """A Windhawk install and data folder laid out as on the owner's PC.
    windhawk.ini is UTF-16 with a BOM, as Windhawk writes it."""
    wh, data = root / 'Windhawk', root / 'ProgramData/Windhawk'
    wh.mkdir(parents=True)
    (wh / 'windhawk.ini').write_bytes(b'\xff\xfe' + INI.format(portable=portable).encode('utf-16-le'))
    (data / 'ModsSource').mkdir(parents=True)
    (data / 'ModsSource/local@taskbar-autohide-any-edge.wh.cpp').write_text('// @version 1.18.0\n')
    (data / 'Engine/Mods/64').mkdir(parents=True)
    (data / 'Engine/Mods/64' / DLL).write_bytes(b'MZ old')
    return str(wh), str(data)


def fake_reg(values):
    return lambda key: dict(values) if key == KEY else {}


@pytest.fixture
def fake_windhawk(tmp_path):
    wh, data = make_windhawk(tmp_path)
    return wh, data, fake_reg({'Version': '1.18.0', 'LibraryFileName': DLL})


def test_layout_ok(fake_windhawk):
    assert wi.layout_problems(*fake_windhawk) == []


def test_layout_portable_refused(tmp_path):
    wh, data = make_windhawk(tmp_path, portable=1)
    assert wi.layout_problems(wh, data, fake_reg({'Version': '1.18.0', 'LibraryFileName': DLL})) == \
        ['Windhawk is in portable mode']


def test_layout_missing_key_refused(fake_windhawk):
    wh, data, _ = fake_windhawk
    problems = wi.layout_problems(wh, data, lambda key: {})
    assert len(problems) == 1 and KEY in problems[0]


def test_layout_missing_dll_and_source_refused(fake_windhawk):
    wh, data, reg = fake_windhawk
    os.remove(os.path.join(data, 'Engine/Mods/64', DLL))
    os.remove(os.path.join(data, 'ModsSource/local@taskbar-autohide-any-edge.wh.cpp'))
    problems = wi.layout_problems(wh, data, reg)
    assert len(problems) == 2
    assert any(DLL in p for p in problems)
    assert any('ModsSource' in p for p in problems)


def test_layout_other_storage_refused(tmp_path):
    wh, data = make_windhawk(tmp_path)
    ini = INI.format(portable=0).replace(r'HKLM\SOFTWARE\Windhawk', r'HKCU\SOFTWARE\Windhawk')
    (tmp_path / 'Windhawk/windhawk.ini').write_bytes(b'\xff\xfe' + ini.encode('utf-16-le'))
    problems = wi.layout_problems(wh, data, fake_reg({'Version': '1.18.0', 'LibraryFileName': DLL}))
    assert problems == [r'windhawk.ini: RegistryKey is HKCU\SOFTWARE\Windhawk, expected HKLM\SOFTWARE\Windhawk']


def test_layout_no_windhawk(tmp_path):
    problems = wi.layout_problems(str(tmp_path / 'none'), str(tmp_path / 'none2'), lambda key: {})
    assert problems and 'windhawk.ini' in problems[0]


def test_reg_query_parses_reg_exe_output(tmp_path, monkeypatch):
    reg = tmp_path / 'reg'
    reg.write_text(f'#!/bin/sh\n[ "$1" = query ] && [ "$2" = \'{KEY}\' ] || exit 1\n'
                   f"printf '%s' '{REG_OUT}'\n")
    reg.chmod(0o755)
    monkeypatch.setenv('REG_CMD', str(reg))
    values = wi.reg_query(KEY)
    assert values['Version'] == '1.18.0'
    assert values['LibraryFileName'] == DLL
    assert values['Exclude'] == ''
    assert 'Settings' not in values
    assert wi.reg_query(KEY + 'x') == {}


def windhawk_compiler():
    wh = os.environ.get('WINDHAWK_DIR', '/mnt/c/Program Files/Windhawk')
    return os.path.isfile(os.path.join(wh, 'Compiler/bin/clang++.exe'))


@pytest.mark.skipif(not windhawk_compiler(), reason="Windhawk's compiler is not installed")
def test_stage_compiles_source(tmp_path):
    with open(os.path.join(REPO, SRC)) as f:
        released = f.read()
    staging = tmp_path / 'staging'
    staging.mkdir()
    (staging / 'result.json').write_text('{"ok": true}')
    wi.stage('1.19.0', str(staging), released)
    assert (staging / 'mod.wh.cpp').read_text() == released
    assert (staging / 'mod.dll').read_bytes()[:2] == b'MZ'
    assert json.loads((staging / 'request.json').read_text()) == {'version': '1.19.0'}
    assert (staging / 'install-mod.ps1').read_bytes() == open(os.path.join(REPO, 'tools/install-mod.ps1'), 'rb').read()
    assert not (staging / 'result.json').exists(), 'old result left behind'


def fake_compiler(tmp, monkeypatch):
    """compile-mod.sh stand-in: writes a DLL-looking file."""
    cc = tmp / 'compile'
    cc.write_text('#!/bin/sh\nprintf MZfake > "$2"\n')
    cc.chmod(0o755)
    monkeypatch.setattr(wi, 'COMPILE', str(cc))


def runtime(root, data, libcxx=b'libc++ new', unwind=b'libunwind new'):
    """Windhawk's compiler runtime, and the copies installed in Mods\\64."""
    bin_ = root / 'Windhawk/Compiler/x86_64-w64-mingw32/bin'
    bin_.mkdir(parents=True)
    (bin_ / 'libc++.dll').write_bytes(libcxx)
    (bin_ / 'libunwind.dll').write_bytes(unwind)
    mods = os.path.join(data, 'Engine/Mods/64')
    with open(os.path.join(mods, 'libc++.whl'), 'wb') as f:
        f.write(b'libc++ new')
    with open(os.path.join(mods, 'libunwind.whl'), 'wb') as f:
        f.write(b'libunwind new')


def test_stage_copies_only_a_runtime_that_differs(tmp_path, monkeypatch):
    wh, data = make_windhawk(tmp_path)
    runtime(tmp_path, data, libcxx=b'libc++ newer')
    monkeypatch.setenv('WINDHAWK_DIR', wh)
    monkeypatch.setenv('WINDHAWK_DATA', data)
    fake_compiler(tmp_path, monkeypatch)
    staging = tmp_path / 'staging'
    staging.mkdir()
    (staging / 'libunwind.whl').write_bytes(b'left from last time')
    wi.stage('1.19.0', str(staging), '// @version 1.19.0\n')
    assert (staging / 'libc++.whl').read_bytes() == b'libc++ newer'
    assert not (staging / 'libunwind.whl').exists(), 'same runtime staged, or an old one left behind'
    assert (staging / 'mod.dll').read_bytes() == b'MZfake'


def test_install_task_runs_hidden():
    # Started after every release: a console must not flash up.
    with open(os.path.join(REPO, 'tools/install-watch-task.ps1')) as f:
        text = f.read()
    action = text[text.index('$installAction ='):text.index('$principal')]
    assert '-Execute "conhost.exe"' in action and '--headless powershell.exe' in action


class Install:
    """install() against a fake Windhawk, registry, compiler and task."""

    def __init__(self, tmp, monkeypatch, task, real_stage=False):
        self.tmp = tmp
        self.staging = tmp / 'staging'
        self.task_log = tmp / 'schtasks.log'
        wh, data = make_windhawk(tmp)
        reg = tmp / 'reg'
        reg.write_text(f"#!/bin/sh\n[ \"$2\" = '{KEY}' ] || exit 1\nprintf '%s' '{REG_OUT}'\n")
        schtasks = tmp / 'schtasks'
        schtasks.write_text(f'#!/bin/sh\necho "$@" >> {self.task_log}\n{task}\n')
        for f in (reg, schtasks):
            f.chmod(0o755)
        for k, v in {'WINDHAWK_DIR': wh, 'WINDHAWK_DATA': data, 'INSTALL_STAGING': str(self.staging),
                     'REG_CMD': str(reg), 'SCHTASKS_CMD': str(schtasks), 'INSTALL_EVERY': '0.05'}.items():
            monkeypatch.setenv(k, v)
        self.staged = []
        if real_stage:
            fake_compiler(tmp, monkeypatch)
        else:
            monkeypatch.setattr(wi, 'stage', self.stage)

    def stage(self, version, staging, source):
        os.makedirs(staging, exist_ok=True)
        self.staged.append((version, staging))


def test_install_reports_task_error(tmp_path, monkeypatch):
    t = Install(tmp_path, monkeypatch, 'printf \'{"ok": false, "error": "1.18.0 is not newer"}\' '
                                       '> "$INSTALL_STAGING/result.json"')
    with pytest.raises(wi.InstallError, match='1.18.0 is not newer'):
        wi.install('1.18.0', '// @version 1.18.0\n')
    assert t.staged == [('1.18.0', str(t.staging))]
    assert t.task_log.read_text() == '/run /tn WindhawkTaskbarModInstall\n'


def test_install_ok(tmp_path, monkeypatch):
    Install(tmp_path, monkeypatch, 'printf \'{"ok": true, "dll": "x.dll"}\' > "$INSTALL_STAGING/result.json"')
    wi.install('1.19.0', '// @version 1.19.0\n')


def test_install_times_out(tmp_path, monkeypatch):
    Install(tmp_path, monkeypatch, 'true')
    monkeypatch.setenv('INSTALL_WAIT', '1')
    with pytest.raises(wi.InstallError, match='timed out'):
        wi.install('1.19.0', '// @version 1.19.0\n')


def test_install_task_missing(tmp_path, monkeypatch):
    Install(tmp_path, monkeypatch, 'echo "ERROR: The system cannot find the file specified." >&2; exit 1')
    with pytest.raises(wi.InstallError, match='install-watch-task.ps1'):
        wi.install('1.19.0', '// @version 1.19.0\n')


def test_install_refuses_odd_layout_before_compiling(tmp_path, monkeypatch):
    t = Install(tmp_path, monkeypatch, 'exit 1')
    monkeypatch.setenv('WINDHAWK_DATA', str(tmp_path / 'elsewhere'))
    with pytest.raises(wi.InstallError, match='ModsSource'):
        wi.install('1.19.0', '// @version 1.19.0\n')
    assert t.staged == []
    assert not t.task_log.exists()


def test_release_then_install_toasts(world, ar, tmp_path, monkeypatch):
    t = Install(tmp_path, monkeypatch, 'printf \'{"ok": true, "dll": "x.dll"}\' > "$INSTALL_STAGING/result.json"')
    world.pr(5)
    v = world.next_version()
    assert ar.release_pending(set()) == [v]
    assert t.staged and t.staged[0][0] == v
    notes = world.notified()
    assert f'v{v} released and installed for taskbar 10.0.26100.9999' in notes
    assert f'releases/tag/v{v}' in notes


def test_install_failure_still_released(world, ar, tmp_path, monkeypatch):
    Install(tmp_path, monkeypatch, 'printf \'{"ok": false, "error": "mod.dll is empty"}\' '
                                   '> "$INSTALL_STAGING/result.json"')
    world.pr(5)
    v = world.next_version()
    assert ar.release_pending(set()) == [v]
    assert git(world.origin, 'cat-file', '-t', f'v{v}') == 'tag'
    notes = world.notified()
    assert f'v{v} released; install it in Windhawk by hand mod.dll is empty' in notes
    assert 'released and installed' not in notes


def test_installed_source_comes_from_the_release_not_the_checkout(world, ar, tmp_path, monkeypatch):
    # The release clone hands over the tagged source; nothing is fetched
    # into, or read from, the owner's checkout.
    t = Install(tmp_path, monkeypatch, 'printf \'{"ok": true, "dll": "x.dll"}\' > "$INSTALL_STAGING/result.json"',
                real_stage=True)
    world.pr(5)
    v = world.next_version()
    assert ar.release_pending(set()) == [v]
    assert (t.staging / 'mod.wh.cpp').read_text() == git(world.origin, 'show', f'v{v}:{SRC}') + '\n'
    assert (t.staging / 'mod.dll').read_bytes() == b'MZfake'
    assert not (world.checkout / '.git/FETCH_HEAD').exists(), 'fetched into the checkout'
    assert git(world.checkout, 'tag', '-l', f'v{v}') == ''
    assert f'v{v} released and installed' in world.notified()
