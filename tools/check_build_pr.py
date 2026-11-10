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
from matcher.apply import _ENTRY, apply_entry, known_builds  # noqa: E402

SRC = 'src/taskbar-autohide-any-edge.wh.cpp'
README = 'README.md'
_VERSION = re.compile(r'(?m)^// @version\s+(\S+)$')


_NOCOLOR = ['--no-color', '--no-ext-diff']


def _git(repo, *args):
    return subprocess.run(['git', '-C', repo, *args], check=True, capture_output=True, text=True,
                          stdin=subprocess.DEVNULL).stdout


def _show(repo, rev, path):
    return _git(repo, 'show', f'{rev}:{path}')


def _pubkey_file(signing_key, tmp):
    """Path to a file holding the public key: the .pub itself, or one derived
    from a private key (CI)."""
    path = os.path.expanduser(signing_key)
    if path.endswith('.pub'):
        open(path).close()
        return path
    out = subprocess.run(['ssh-keygen', '-y', '-P', '', '-f', path], check=True, capture_output=True,
                         text=True, stdin=subprocess.DEVNULL).stdout
    derived = os.path.join(tmp, 'signing.pub')
    with open(derived, 'w') as f:
        f.write(out)
    return derived


def _signature_problems(repo, base, head, signing_key):
    with tempfile.TemporaryDirectory() as tmp:
        try:
            pubfile = _pubkey_file(signing_key, tmp)
            pub = ' '.join(open(pubfile).read().split()[:2])
            fp = next(t for t in subprocess.run(['ssh-keygen', '-lf', pubfile], check=True, capture_output=True,
                                                text=True, stdin=subprocess.DEVNULL).stdout.split()
                      if t.startswith('SHA256:'))
        except (OSError, subprocess.CalledProcessError, StopIteration) as e:
            return [f'cannot read the signing key {signing_key}: {e}']
        allowed = os.path.join(tmp, 'allowed')
        with open(allowed, 'w') as f:
            f.write(f'* namespaces="git" {pub}\n')
        out = _git(repo, '-c', f'gpg.ssh.allowedSignersFile={allowed}',
                   '-c', 'gpg.openpgp.program=/bin/false', '-c', 'gpg.x509.program=/bin/false',
                   'log', '--no-show-signature', '--format=%H %G? %GK', f'{base}..{head}')
    bad = []
    for line in out.splitlines():
        h, g, *k = line.split()
        if g != 'G' or k != [fp]:
            bad.append(f'commit {h[:12]} is not signed by this PC')
    return bad


def _builds(repo, rev):
    with tempfile.NamedTemporaryFile('w', suffix='.wh.cpp') as f:
        f.write(_show(repo, rev, SRC))
        f.flush()
        return known_builds(f.name)


def _readme_changes(repo, base, head):
    """(added lines, removed lines) of README.md, without the diff headers."""
    added, removed, in_hunk = [], [], False
    for l in _git(repo, 'diff', *_NOCOLOR, '--unified=0', base, head, '--', README).splitlines():
        if l.startswith('@@'):
            in_hunk = True
        elif in_hunk and l.startswith('+'):
            added.append(l[1:])
        elif in_hunk and l.startswith('-'):
            removed.append(l[1:])
    return added, removed


def _entry_text(src, version):
    """The new entry's own lines, as they stand in the file."""
    m = next(m for m in _ENTRY.finditer(src) if m['version'] == version)
    start = src.rindex('\n', 0, m.start()) + 1
    return src[start:src.index('\n', m.end()) + 1]


def _beyond_entry(repo, base, head, version, ref):
    """True if head differs from base + exactly this entry as apply_entry writes it."""
    with tempfile.TemporaryDirectory() as tmp:
        src, readme = os.path.join(tmp, 'mod.wh.cpp'), os.path.join(tmp, 'README.md')
        for path, name in ((src, SRC), (readme, README)):
            with open(path, 'w') as f:
                f.write(_show(repo, base, name))
        entry = _entry_text(_show(repo, head, SRC), version)
        apply_entry(src, readme, entry, version, ref)
        return (open(src).read() != _show(repo, head, SRC)
                or open(readme).read() != _show(repo, head, README))


def problems(repo, base, head, signing_key):
    out = _signature_problems(repo, base, head, signing_key)

    names = [n for n in _git(repo, 'diff', *_NOCOLOR, '--name-only', base, head).splitlines()
             if n not in (SRC, README)]
    if names:
        out.append(f'changes files other than src/ and README.md: {", ".join(names)}')

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
    dupes = [b['version'] for b in new if b['version'] in old_by and
             sum(1 for x in new if x['version'] == b['version']) > 1]
    if dupes:
        out.append(f'adds a duplicate of build {dupes[0]}')
    elif len(new) - len(old) != len(added) or len(added) != 1:
        out.append(f'adds {len(new) - len(old)} builds, expected 1')

    m_old, m_new = (_VERSION.search(_show(repo, r, SRC)) for r in (base, head))
    try:
        major, minor, _ = m_old[1].split('.')
        want = f'{major}.{int(minor) + 1}.0'
    except (TypeError, ValueError):
        want = None
    if not m_new or not want or m_new[1] != want:
        out.append(f'version {m_new[1] if m_new else None} is not the next minor {want} after {m_old[1] if m_old else None}')

    plus, minus = _readme_changes(repo, base, head)
    if len(added) == 1:
        ok = len(plus) == 1 and not minus and plus[0].startswith(f'| {added[0]["version"]} |')
    else:
        ok = len(plus) == 1 and not minus
    if not ok:
        out.append('README.md must add exactly one build row')

    # Check 5: head is exactly base plus this one entry, byte for byte.
    if not dupes and len(added) == 1 and not names:
        row = re.fullmatch(r'\| (10\.0\.\d+\.\d+) \| (10\.0\.\d+\.\d+) \|', plus[0]) if len(plus) == 1 else None
        try:
            if not row or row[1] != added[0]['version']:
                raise ValueError('no reference version')
            if _beyond_entry(repo, base, head, added[0]['version'], row[2]):
                out.append(BEYOND)
        except (ValueError, StopIteration, subprocess.CalledProcessError):
            out.append(BEYOND)
    return out


BEYOND = 'src/ or README.md has changes beyond the new build entry'


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
