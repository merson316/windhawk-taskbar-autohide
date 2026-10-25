"""Runs at sign-in (see install-watch-task.ps1). If Windows has installed a
taskbar.dll that the mod cannot handle -- no published symbols, not in
kKnownBuilds, no PR yet -- match it and open a draft PR for the owner to test.

Exit codes: 0 nothing to do or PR opened, 3 match failed, 4 signing
unavailable, 5 symbol server unreachable."""
import os
import shlex
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from matcher import HOOKED  # noqa: E402
from matcher.apply import apply_entry, known_builds  # noqa: E402
from matcher.match import evidence_table, format_entry, match_build  # noqa: E402
from matcher.pdb import publics  # noqa: E402
from matcher.pe import PE  # noqa: E402
from matcher.reference import auto_reference  # noqa: E402

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
    key = os.path.expanduser(git('config', 'user.signingkey', check=False).stdout.strip())
    if not key:
        return False
    if not key.startswith('ssh-'):
        path = key if key.endswith('.pub') or not os.path.exists(key + '.pub') else key + '.pub'
        try:
            key = open(path).read()
        except OSError:
            return False
    blob = key.split()[1] if len(key.split()) > 1 else key
    agent = subprocess.run(['ssh-add', '-L'], capture_output=True, text=True)
    return agent.returncode == 0 and blob in agent.stdout


def main(argv):
    dry_run = '--dry-run' in argv
    pe = PE(DLL)
    version, key = pe.file_version(), pe.pdb_key()

    if any(e['pdb_key'] == key for e in known_builds(SRC)):
        print(f'known: {version}')
        return 0
    try:
        published, status = pdb_published(key)
    except (urllib.error.URLError, OSError) as e:
        print(f'symbol server unreachable: {e}')
        return 5
    if published:
        print(f'symbols published ({status}): {version}')
        return 0
    branch = f'build/{version}'
    if git('rev-parse', '--verify', '-q', f'refs/heads/{branch}', check=False).returncode == 0 or \
            git('ls-remote', '--exit-code', '--heads', 'origin', branch, check=False).returncode == 0:
        print(f'pending PR: {branch}')
        return 0

    ref, ref_pdb = auto_reference(pe)
    found = publics(ref_pdb, list(HOOKED.values()))
    result = match_build(pe, ref, {f: found[n] for f, n in HOOKED.items()})
    evidence = evidence_table(result)
    print(f'new {version}, reference {ref.file_version()}\n{evidence}')
    if result.rvas is None:
        notify(f'Taskbar {version}: automatic match failed', result.reason)
        return 3
    entry = format_entry(pe, result.rvas)
    print(entry)

    if dry_run:
        print(f'would open draft PR "taskbar.dll {version}: built-in addresses" from {branch}')
        return 0
    if not signing_available():
        notify(f'Taskbar {version} matched but signing is unavailable',
               'run bin/keys-load.sh wsl, then tools/watch-build.sh')
        return 4

    git('fetch', '-q', 'origin')
    git('switch', '-q', '-c', branch, 'origin/main')
    mod_version = apply_entry(SRC, README, entry, version, ref.file_version())
    git('commit', '-q', '-S', '-am', f'feat: built-in addresses for taskbar.dll {version}\n\n'
        f'Matched against {ref.file_version()} by tools/match-build.py.\n\n{evidence}\n')
    git('push', '-q', '-u', 'origin', branch)
    body = open(TEMPLATE).read().format(version=version, reference=ref.file_version(),
                                        mod_version=mod_version, evidence=evidence)
    with tempfile.NamedTemporaryFile('w', suffix='.md', delete=False) as f:
        f.write(body)
    url = subprocess.run(['gh', 'pr', 'create', '--draft', '--head', branch,
                          '--title', f'taskbar.dll {version}: built-in addresses', '--body-file', f.name],
                         capture_output=True, text=True, check=True).stdout.strip()
    os.unlink(f.name)
    git('switch', '-q', '-')
    notify(f'Taskbar {version}: draft PR ready to test', url)
    print(url)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
