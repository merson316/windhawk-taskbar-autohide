"""Decides whether a watcher-made build PR is safe to release automatically:
every commit is signed by this PC's key, the mod gains exactly one
kKnownBuilds entry and no other, the version is the next minor, and the
README gains exactly one build row.

Usage: check_build_pr.py BASE HEAD    (run in the repo; prints each problem,
exit 1 if any)."""
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from matcher.apply import known_builds  # noqa: E402

SRC = 'src/taskbar-autohide-any-edge.wh.cpp'
README = 'README.md'
_VERSION = re.compile(r'(?m)^// @version\s+(\S+)$')


def _git(repo, *args):
    return subprocess.run(['git', '-C', repo, *args], check=True, capture_output=True, text=True).stdout


def _show(repo, rev, path):
    return _git(repo, 'show', f'{rev}:{path}')


def _public_key(signing_key):
    if signing_key.endswith('.pub'):
        text = open(os.path.expanduser(signing_key)).read().split()
        return ' '.join(text[:2])
    out = subprocess.run(['ssh-keygen', '-y', '-f', os.path.expanduser(signing_key)],
                         check=True, capture_output=True, text=True).stdout
    return ' '.join(out.split()[:2])


def _signature_problems(repo, base, head, signing_key):
    try:
        pub = _public_key(signing_key)
    except (OSError, subprocess.CalledProcessError, IndexError) as e:
        return [f'cannot read the signing key {signing_key}: {e}']
    with tempfile.NamedTemporaryFile('w', suffix='.allowed') as f:
        f.write(f'* namespaces="git" {pub}\n')
        f.flush()
        out = _git(repo, '-c', f'gpg.ssh.allowedSignersFile={f.name}',
                   'log', '--format=%H %G?', f'{base}..{head}')
    return [f'commit {h[:12]} is not signed by this PC' for h, g in (l.split() for l in out.splitlines()) if g != 'G']


def _builds(repo, rev):
    with tempfile.NamedTemporaryFile('w', suffix='.wh.cpp') as f:
        f.write(_show(repo, rev, SRC))
        f.flush()
        return known_builds(f.name)


def problems(repo, base, head, signing_key):
    out = _signature_problems(repo, base, head, signing_key)

    try:
        old, new = _builds(repo, base), _builds(repo, head)
    except (subprocess.CalledProcessError, ValueError) as e:
        return out + [f'cannot read kKnownBuilds: {e}']
    old_by = {b['version']: b for b in old}
    new_by = {b['version']: b for b in new}
    for v, b in old_by.items():
        if v not in new_by:
            out.append(f'removes existing build {v}')
        elif new_by[v] != b:
            out.append(f'changes existing build {v}')
    added = [b for b in new if b['version'] not in old_by]
    if len(new) - len(old) != len(added) or len(added) != 1:
        out.append(f'adds {len(new) - len(old)} builds, expected 1')

    m_old, m_new = (_VERSION.search(_show(repo, r, SRC)) for r in (base, head))
    try:
        major, minor, _ = m_old[1].split('.')
        want = f'{major}.{int(minor) + 1}.0'
    except (TypeError, ValueError):
        want = None
    if not m_new or not want or m_new[1] != want:
        out.append(f'version {m_new[1] if m_new else None} is not the next minor {want} after {m_old[1] if m_old else None}')

    diff = _git(repo, 'diff', '--unified=0', base, head, '--', README).splitlines()
    plus = [l[1:] for l in diff if l.startswith('+') and not l.startswith('+++')]
    minus = [l for l in diff if l.startswith('-') and not l.startswith('---')]
    if len(added) == 1:
        ok = len(plus) == 1 and not minus and plus[0].startswith(f'| {added[0]["version"]} |')
    else:
        ok = len(plus) == 1 and not minus
    if not ok:
        out.append('README.md must add exactly one build row')
    return out


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    key = subprocess.run(['git', 'config', 'user.signingkey'], capture_output=True, text=True).stdout.strip()
    found = problems(os.getcwd(), argv[1], argv[2], key)
    for p in found:
        print(p)
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
