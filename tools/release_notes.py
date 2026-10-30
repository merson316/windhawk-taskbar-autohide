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
from matcher.buildcheck import accepted_layouts, os_builds  # noqa: E402
from matcher.pe import PE  # noqa: E402
from matcher.reference import pdb_published  # noqa: E402

COMPAT = 'docs/compatibility.json'
BUILDS = 'docs/builds.json'
SRC = 'src/taskbar-autohide-any-edge.wh.cpp'
OTHER_BUILDS = ("Other builds: works wherever Windhawk can download the taskbar's symbols, "
                'unless an update changes the code the mod checks; in that case the mod shows '
                'a warning instead of hooking.')
WINVER_TIP = 'Find your OS build: press Win+R, type `winver`, and read the number after "OS Build".'
SUPPORT = {'built-in': 'Built-in addresses (Microsoft has not published symbols)', 'symbols': "Microsoft's symbols"}
SHOWN = 5
INSTALL = ('Download `taskbar-autohide-any-edge.wh.cpp` below, then in Windhawk: '
           'Create a new mod → replace the contents → Compile → Enable.')


def should_work_for(layouts, builds, tested_taskbars, known=(), published=()):
    """Builds this version should handle, newest first: symbols published and
    resolving with code passing this version's load-time checks, plus the
    builds its kKnownBuilds table carries. Builds already tested are left out.
    A table build whose PDB key is in `published` is resolved from symbols
    now, so it is labelled that way, and listed once."""
    ok = [b for b in builds if b['symbols'] and b['resolved'] and b['durations']
          and b['layout'] in layouts and b['taskbar'] not in tested_taskbars]
    listed = {b['taskbar'] for b in ok}
    ok += [{'taskbar': k['version'], 'built_in': k['pdb_key'] not in published} for k in known
           if k['version'] not in tested_taskbars and k['version'] not in listed]
    return sorted(ok, key=lambda b: int(b['taskbar'].rsplit('.', 1)[1]), reverse=True)


def _build_rows(builds, how):
    if not how:
        return ['| OS build (winver) | taskbar.dll |', '|---|---|'] + \
            [f'| {os_builds(b["taskbar"])} | {b["taskbar"]} |' for b in builds]
    return ['| OS build (winver) | taskbar.dll | How the mod finds the taskbar |', '|---|---|---|'] + \
        [f'| {os_builds(b["taskbar"])} | {b["taskbar"]} | '
         f'{"Built-in addresses" if b.get("built_in") else SUPPORT["symbols"]} |' for b in builds]


def render(version, compat, changes, should_work=()):
    out = ['## Works with', '']
    tested = [c for c in compat if version in c['tested_with']]
    if tested:
        out += ['**Tested on**', '', '| OS build (winver) | taskbar.dll | How the mod finds the taskbar |',
                '|---|---|---|']
        out += [f'| {c["os_build"]} | {c["taskbar"]} | {SUPPORT[c["support"]]} |' for c in tested]
        out.append('')
    if should_work:
        out += ['**Should work on**: builds whose symbols Microsoft has published and whose code passes '
                'the checks this version runs when it loads. Not tested by hand.', '']
        # The approved layout has two columns; a third appears only when some
        # rows rely on built-in addresses rather than symbols.
        how = any(b.get('built_in') for b in should_work)
        out += _build_rows(should_work[:SHOWN], how) + ['']
        older = should_work[SHOWN:]
        if older:
            out += ['<details><summary>%d older builds</summary>' % len(older), '']
            out += _build_rows(older, how) + ['', '</details>', '']
    out += [OTHER_BUILDS, '', WINVER_TIP, '', '## Changes', '']
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


def source_at(version):
    tag = f'v{version}'
    if not _git('tag', '-l', tag):
        return open(SRC).read()
    return _git('show', f'{tag}:{SRC}') + '\n'


def known_at(version):
    with tempfile.NamedTemporaryFile('w', suffix='.cpp', delete=False) as f:
        f.write(source_at(version))
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
    builds = json.load(open(BUILDS)) if os.path.exists(BUILDS) else []
    tested = {c['taskbar'] for c in compat if args.version in c['tested_with']}
    known = known_at(args.version)
    published = {k['pdb_key'] for k in known if pdb_published(k['pdb_key'])[0]}
    should_work = should_work_for(accepted_layouts(source_at(args.version)), builds, tested, known, published)
    print(render(args.version, compat, changes, should_work))


if __name__ == '__main__':
    main()
