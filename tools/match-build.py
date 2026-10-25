#!/usr/bin/env python3
"""Find the mod's hooked functions in a taskbar.dll whose symbols Microsoft has
not published, by matching against an older build that has a PDB.

  tools/match-build.py --new DLL --ref DLL --ref-pdb PDB
  tools/match-build.py --new DLL --auto-ref

Exit 0 with the evidence table on a match; exit 2, with the reason on stderr,
when any function cannot be matched with certainty."""
import argparse
import glob
import json
import os
import sys
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from matcher import HOOKED  # noqa: E402
from matcher.match import evidence_table, format_entry, match_build, pick_reference  # noqa: E402
from matcher.pdb import publics  # noqa: E402
from matcher.pe import PE  # noqa: E402

WINSXS = os.environ.get('WINSXS', '/mnt/c/Windows/WinSxS')
SYMBOLS = os.environ.get('WINDHAWK_SYMBOLS', '/mnt/c/ProgramData/Windhawk/Engine/Symbols/Taskbar.pdb')
MSDL = os.environ.get('MSDL_BASE', 'https://msdl.microsoft.com/download/symbols')
CACHE = os.path.expanduser('~/.cache/taskbar-matcher')


def fetch_pdb(key):
    """A PDB for this key from Windhawk's cache, ours, or msdl; None if unpublished."""
    for path in (f'{SYMBOLS}/{key}/Taskbar.pdb', f'{CACHE}/{key}/Taskbar.pdb'):
        if os.path.exists(path):
            return path
    req = urllib.request.Request(f'{MSDL}/Taskbar.pdb/{key}/Taskbar.pdb',
                                 headers={'User-Agent': 'Microsoft-Symbol-Server/10.0.0.0'})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            data = r.read()
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise
    os.makedirs(f'{CACHE}/{key}', exist_ok=True)
    path = f'{CACHE}/{key}/Taskbar.pdb'
    with open(path, 'wb') as f:
        f.write(data)
    return path


def auto_reference(new):
    cands = []
    for dll in glob.glob(f'{WINSXS}/amd64_microsoft-windows-taskbar-dll_*/Taskbar.dll'):
        pe = PE(dll)
        if pe.pdb_key() == new.pdb_key():
            continue
        pdb = fetch_pdb(pe.pdb_key())
        if pdb:
            cands.append((pe, pdb))
    return pick_reference(new, cands)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--new', required=True)
    ap.add_argument('--ref')
    ap.add_argument('--ref-pdb')
    ap.add_argument('--auto-ref', action='store_true')
    ap.add_argument('--json')
    ap.add_argument('--entry')
    args = ap.parse_args()

    new = PE(args.new)
    if args.auto_ref:
        ref, ref_pdb = auto_reference(new)
    elif args.ref and args.ref_pdb:
        ref, ref_pdb = PE(args.ref), args.ref_pdb
    else:
        ap.error('give --ref and --ref-pdb, or --auto-ref')

    found = publics(ref_pdb, list(HOOKED.values()))
    missing = [f for f, n in HOOKED.items() if n not in found]
    if missing:
        print(f'reference PDB lacks {", ".join(missing)}', file=sys.stderr)
        return 2
    result = match_build(new, ref, {f: found[n] for f, n in HOOKED.items()})

    print(f'new {new.file_version()} ({new.pdb_key()}), reference {ref.file_version()} ({ref.pdb_key()})')
    print(evidence_table(result))
    if args.json:
        with open(args.json, 'w') as f:
            json.dump({'new': new.file_version(), 'reference': ref.file_version(),
                       'rvas': result.rvas, 'reason': result.reason,
                       'rows': [r.__dict__ for r in result.rows]}, f, indent=2)
    if result.rvas is None:
        print(f'no match: {result.reason}', file=sys.stderr)
        return 2
    if args.entry:
        with open(args.entry, 'w') as f:
            f.write(format_entry(new, result.rvas))
    return 0


if __name__ == '__main__':
    sys.exit(main())
