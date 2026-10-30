"""Runs at sign-in and hourly (see install-watch-task.ps1). Looks at the
running taskbar.dll and at any newer one Windows has staged in WinSxS for the
next restart, so a problem is known before the restart rather than after.

For each: if Microsoft has published its symbols, check the mod's load checks
pass on it, and warn once if they will not. If not -- and it is not in
kKnownBuilds and has no PR yet -- match it and open a draft PR for the owner
to test.

All git work happens in a temporary worktree of origin/main: the owner's
checkout, and any uncommitted edits in it, are never touched. Each warning is
raised once (see notify_once), so the hourly runs do not repeat it.

Exit codes: 0 nothing to do or PR opened, 3 match failed, 4 signing
unavailable, 5 symbol server unreachable or PDB not downloadable yet, 6
anything else failed (notified), 7 a published build fails the mod's checks."""
import datetime
import glob
import os
import shlex
import struct
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from matcher import HOOKED, reference  # noqa: E402
from matcher.apply import apply_entry, known_builds  # noqa: E402
from matcher.buildcheck import accepted_layouts, check_build  # noqa: E402
from matcher.match import evidence_table, format_entry, match_build  # noqa: E402
from matcher.pdb import publics  # noqa: E402
from matcher.pe import PE  # noqa: E402
from matcher.reference import auto_reference, fetch_pdb  # noqa: E402

REPO = os.getcwd()
SRC = 'src/taskbar-autohide-any-edge.wh.cpp'
README = 'README.md'
TEMPLATE = '.github/pull_request_template/new-build.md'
DLL = os.environ.get('TASKBAR_DLL', '/mnt/c/Windows/System32/taskbar.dll')
MSDL = os.environ.get('MSDL_BASE', 'https://msdl.microsoft.com/download/symbols')


def notify(title, message):
    cmd = os.environ.get('NOTIFY_CMD')
    if cmd:
        argv = shlex.split(cmd) + [title, message]
    else:
        toast = subprocess.run(['wslpath', '-w', os.path.join(REPO, 'tools/toast.ps1')],
                               capture_output=True, text=True).stdout.strip()
        argv = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', toast,
                '-Title', title, '-Message', message]
    subprocess.run(argv, check=False)


def notify_once(tag, title, message):
    """notify, unless a warning with this tag was already raised."""
    state = os.path.join(reference.CACHE, 'notified')
    seen = open(state).read().split() if os.path.exists(state) else []
    if tag in seen:
        return
    notify(title, message)
    os.makedirs(reference.CACHE, exist_ok=True)
    with open(state, 'a') as f:
        f.write(tag + '\n')


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def pdb_published(key):
    req = urllib.request.Request(f'{MSDL}/Taskbar.pdb/{key}/Taskbar.pdb', method='HEAD',
                                 headers={'User-Agent': 'Microsoft-Symbol-Server/10.0.0.0'})
    try:
        status = urllib.request.build_opener(_NoRedirect).open(req, timeout=30).status
    except urllib.error.HTTPError as e:
        status = e.code
    return status in (200, 302), status


def git(*args, check=True):
    return subprocess.run(['git', *args], capture_output=True, text=True, check=check)


def signing_available():
    """Sign a scratch file exactly as git would, through gpg.ssh.program (the
    owner's wrapper picks its own agent, so asking ssh-add proves nothing)."""
    key = os.path.expanduser(git('config', 'user.signingkey', check=False).stdout.strip())
    program = git('config', 'gpg.ssh.program', check=False).stdout.strip() or 'ssh-keygen'
    if not key:
        return False
    with tempfile.TemporaryDirectory() as d:
        probe = os.path.join(d, 'probe')
        with open(probe, 'w') as f:
            f.write('signing probe\n')
        try:
            return subprocess.run([os.path.expanduser(program), '-Y', 'sign', '-n', 'git', '-f', key, probe],
                                  capture_output=True, timeout=30).returncode == 0
        except (OSError, subprocess.TimeoutExpired):
            return False


def known_on_main():
    """kKnownBuilds as origin/main has it, not as the checkout happens to be."""
    with tempfile.NamedTemporaryFile('w', suffix='.cpp', delete=False) as f:
        f.write(git('show', f'origin/main:{SRC}').stdout)
    try:
        return known_builds(f.name)
    finally:
        os.unlink(f.name)


def open_pr(version, branch, ref_version, entry, evidence):
    """Commit the entry on a new branch from origin/main in a temporary
    worktree, push it and open a draft PR. Undoes the branch on failure so
    the next sign-in tries again."""
    tree = tempfile.mkdtemp(prefix='taskbar-watch-')
    pushed = False
    git('worktree', 'add', '-q', '-b', branch, tree, 'origin/main')
    try:
        mod_version = apply_entry(os.path.join(tree, SRC), os.path.join(tree, README),
                                  entry, version, ref_version)
        git('-C', tree, 'commit', '-q', '-S', '-m',
            f'feat: built-in addresses for taskbar.dll {version}\n\n'
            f'Matched against {ref_version} by tools/match-build.py.\n\n{evidence}\n', '--', SRC, README)
        git('-C', tree, 'push', '-q', '-u', 'origin', branch)
        pushed = True
        body = open(TEMPLATE).read().format(version=version, reference=ref_version,
                                            mod_version=mod_version, evidence=evidence)
        with tempfile.NamedTemporaryFile('w', suffix='.md', delete=False) as f:
            f.write(body)
        try:
            return subprocess.run(['gh', 'pr', 'create', '--draft', '--head', branch,
                                   '--title', f'taskbar.dll {version}: built-in addresses', '--body-file', f.name],
                                  capture_output=True, text=True, check=True, cwd=tree).stdout.strip()
        finally:
            os.unlink(f.name)
    except BaseException:
        if pushed:
            git('push', '-q', 'origin', '--delete', branch, check=False)
        raise
    finally:
        # The pushed branch is what matters; leave the owner's checkout as it was.
        git('worktree', 'remove', '--force', tree, check=False)
        git('worktree', 'prune', check=False)
        git('branch', '-q', '-D', branch, check=False)


def _version(pe):
    return tuple(int(x) for x in pe.file_version().split('.'))


def staged_build(current):
    """The newest taskbar.dll in WinSxS if it is newer than the running one:
    an update has staged it, and it takes over at the next restart."""
    newest = None
    for path in glob.glob(f'{reference.WINSXS}/amd64_microsoft-windows-taskbar-dll_*/Taskbar.dll'):
        try:
            pe = PE(path)
            pe.pdb_key()
        except (OSError, ValueError, struct.error):
            continue
        if _version(pe) > _version(current) and (newest is None or _version(pe) > _version(newest)):
            newest = pe
    return newest


def check_published(pe, key, layouts, staged):
    """Symbols are out, so the mod resolves this build itself; what remains is
    whether its load checks pass on it."""
    version = pe.file_version()
    try:
        pdb = fetch_pdb(key)
    except (urllib.error.URLError, OSError, ValueError) as e:
        pdb, why = None, e
    else:
        why = 'not served yet'
    if not pdb:
        print(f'symbols published but the PDB could not be downloaded ({why}); trying next time: {version}')
        return 5
    r = check_build(pe, pdb)
    problems = []
    if not r['resolved']:
        problems.append('its symbols lack a function the mod hooks')
    if r['layout'] not in layouts:
        problems.append('GetAutoHideFlags changed shape, so ScanForOffsets will refuse it')
    if not r['durations']:
        problems.append('SlideWindow no longer loads the slide durations where the mod reads them')
    if not problems:
        print(f'symbols published, mod checks pass: {version}')
        return 0
    when = 'after the next restart' if staged else 'now'
    message = f'The mod will refuse to load {when}: ' + '; '.join(problems) + '.'
    print(message)
    notify_once(f'breaks:{key}', f'Taskbar {version} will break auto-hide', message)
    return 7


def main(argv):
    dry_run = '--dry-run' in argv
    running = PE(DLL)

    git('fetch', '-q', '--no-tags', 'origin')
    known = known_on_main()
    layouts = accepted_layouts(git('show', f'origin/main:{SRC}').stdout)
    builds = [(running, False)]
    staged = staged_build(running)
    if staged:
        print(f'staged for restart: {staged.file_version()}')
        builds.append((staged, True))
    codes = [check(pe, is_staged, known, layouts, dry_run) for pe, is_staged in builds]
    return next((c for c in codes if c), 0)


def check(pe, staged, known, layouts, dry_run):
    version, key = pe.file_version(), pe.pdb_key()
    if any(e['pdb_key'] == key for e in known):
        print(f'known: {version}')
        return 0
    try:
        published, status = pdb_published(key)
    except (urllib.error.URLError, OSError) as e:
        print(f'symbol server unreachable: {e}')
        return 5
    if published:
        return check_published(pe, key, layouts, staged)
    if status != 404:
        # A 503 or 429 says nothing about the build; try again next sign-in.
        print(f'symbol server answered {status}; trying again next time')
        return 5
    branch = f'build/{version}'
    if git('ls-remote', '--exit-code', '--heads', 'origin', branch, check=False).returncode == 0:
        print(f'pending PR: {branch}')
        return 0

    ref, ref_pdb = auto_reference(pe)
    found = publics(ref_pdb, list(HOOKED.values()))
    missing = [f for f, n in HOOKED.items() if n not in found]
    if missing:
        raise RuntimeError(f'reference {ref.file_version()} PDB lacks {", ".join(missing)}')
    result = match_build(pe, ref, {f: found[n] for f, n in HOOKED.items()})
    evidence = evidence_table(result)
    print(f'new {version}, reference {ref.file_version()}\n{evidence}')
    if result.rvas is None:
        notify_once(f'nomatch:{key}', f'Taskbar {version}: automatic match failed', result.reason)
        return 3
    entry = format_entry(pe, result.rvas)
    print(entry)

    if dry_run:
        print(f'would open draft PR "taskbar.dll {version}: built-in addresses" from {branch}')
        return 0
    if not signing_available():
        notify_once(f'nosign:{key}:{datetime.date.today()}', f'Taskbar {version} matched but signing is unavailable',
               'run bin/keys-load.sh wsl, then tools/watch-build.sh')
        return 4
    url = open_pr(version, branch, ref.file_version(), entry, evidence)
    notify(f'Taskbar {version}: draft PR ready to test', url)
    print(url)
    return 0


def run(argv):
    try:
        return main(argv)
    except Exception as e:   # unattended: never fail without telling the owner
        print(f'watcher failed: {e!r}', file=sys.stderr)
        # Once a day per kind of failure: a run every hour must not toast every hour.
        notify_once(f'failed:{type(e).__name__}:{datetime.date.today()}', 'Taskbar watcher failed',
                    f'{type(e).__name__}: {e} (see ~/.cache/taskbar-matcher/watch.log)')
        return 6


if __name__ == '__main__':
    sys.exit(run(sys.argv[1:]))
