#!/usr/bin/env python3
"""Check a kKnownBuilds entry against the real PDB once Microsoft publishes it.

  tools/verify-known-build.py --src src/taskbar-autohide-any-edge.wh.cpp --pdb Taskbar.pdb --version 10.0.26100.9807

Prints a markdown table and exits 0 when all seven addresses match, 1 when any
differs."""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from matcher import HOOKED  # noqa: E402
from matcher.apply import known_builds  # noqa: E402
from matcher.pdb import publics  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--src', required=True)
    ap.add_argument('--pdb', required=True)
    ap.add_argument('--version', required=True)
    args = ap.parse_args()

    entry = next((e for e in known_builds(args.src) if e['version'] == args.version), None)
    if entry is None:
        print(f'{args.version} is not in kKnownBuilds', file=sys.stderr)
        return 2
    found = publics(args.pdb, list(HOOKED.values()))
    print('| Function | Expected | Actual | Match |')
    print('|---|---|---|---|')
    ok = True
    for field, name in HOOKED.items():
        actual = found.get(name)
        match = actual == entry[field]
        ok &= match
        shown = f'0x{actual:X}' if actual is not None else 'not found'
        print(f'| {field} | 0x{entry[field]:X} | {shown} | {"yes" if match else "no"} |')
    print()
    print('All seven match.' if ok else 'Some addresses differ: correct kKnownBuilds before more users rely on them.')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
