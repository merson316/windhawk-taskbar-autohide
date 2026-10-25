#!/usr/bin/env python3
"""Release notes for one mod version: which Windows builds it works with, what
changed, and how to install it.

  tools/release_notes.py --version 1.17.0 [--changes FILE]

Without --changes, the Changes section lists the commit subjects since the
previous tag, as a draft for the owner to rewrite in plain language."""
import argparse
import json
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from matcher.apply import known_builds  # noqa: E402
from matcher.pe import PE  # noqa: E402

COMPAT = 'docs/compatibility.json'
SRC = 'src/taskbar-autohide-any-edge.wh.cpp'
OTHER_BUILDS = ("Other builds: works wherever Windhawk can download the taskbar's symbols, "
                'unless an update changes the code the mod checks; in that case the mod shows '
                'a warning instead of hooking.')
INSTALL = ('Download `taskbar-autohide-any-edge.wh.cpp` below, then in Windhawk: '
           'Create a new mod → replace the contents → Compile → Enable.')


def render(version, compat, known, changes):
    out = ['## Works with', '']
    tested = [c for c in compat if version in c['tested_with']]
    if tested:
        out += ['Tested on:', '', '| OS build | taskbar.dll | Support |', '|---|---|---|']
        out += [f'| {c["os_build"]} | {c["taskbar"]} | {c["support"]} |' for c in tested]
        out.append('')
    if known:
        out += ['Built-in support for builds without published symbols:', '']
        out += [f'- taskbar.dll {k["version"]}' for k in known]
        out.append('')
    out += [OTHER_BUILDS, '', '## Changes', '']
    out += [f'- {c}' for c in changes]
    out += ['', '## Install', '', INSTALL, '']
    return '\n'.join(out)


def record_tested(compat_path, os_build, dll_path, src_path, version):
    """Note that `version` was tested on this OS build, with the taskbar.dll at
    dll_path served either by symbols or by kKnownBuilds."""
    compat = json.load(open(compat_path))
    pe = PE(dll_path)
    support = 'built-in' if any(k['pdb_key'] == pe.pdb_key() for k in known_builds(src_path)) else 'symbols'
    row = next((c for c in compat if c['os_build'] == os_build), None)
    if row is None:
        row = {'os_build': os_build, 'taskbar': pe.file_version(), 'support': support, 'tested_with': []}
        compat.append(row)
    if version not in row['tested_with']:
        row['tested_with'].append(version)
    with open(compat_path, 'w') as f:
        json.dump(compat, f, indent=2)
        f.write('\n')


def _git(*args):
    return subprocess.run(['git', *args], capture_output=True, text=True).stdout.strip()


def draft_changes(version):
    tag = f'v{version}'
    end = tag if _git('tag', '-l', tag) else 'HEAD'
    prev = _git('describe', '--tags', '--abbrev=0', f'{end}^')
    span = f'{prev}..{end}' if prev else end
    return [s for s in _git('log', '--format=%s', span).splitlines() if s]


def known_at(version):
    tag = f'v{version}'
    if not _git('tag', '-l', tag):
        return known_builds(SRC)
    with tempfile.NamedTemporaryFile('w', suffix='.cpp', delete=False) as f:
        f.write(_git('show', f'{tag}:{SRC}') + '\n')
    try:
        return known_builds(f.name)
    except ValueError:      # versions before 1.17.0 have no table
        return []
    finally:
        os.unlink(f.name)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--version', required=True)
    ap.add_argument('--changes')
    ap.add_argument('--record', metavar='OS_BUILD', help='record this version as tested on OS_BUILD and exit')
    ap.add_argument('--dll', default='/mnt/c/Windows/System32/taskbar.dll')
    args = ap.parse_args()
    if args.record:
        record_tested(COMPAT, args.record, args.dll, SRC, args.version)
        return
    compat = json.load(open(COMPAT)) if os.path.exists(COMPAT) else []
    changes = ([l.strip().lstrip('- ') for l in open(args.changes) if l.strip()]
               if args.changes else draft_changes(args.version))
    print(render(args.version, compat, known_at(args.version), changes))


if __name__ == '__main__':
    main()
