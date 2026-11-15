#!/usr/bin/env python3
"""Check a kKnownBuilds entry against the real PDB once Microsoft publishes it.

  tools/verify-known-build.py --src src/taskbar-autohide-any-edge.wh.cpp --pdb Taskbar.pdb --version 10.0.26100.9807

Prints a markdown table and exits 0 when every address in the entry matches, 1
when any differs. An optional address the entry leaves out (0) is shown and not
counted."""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from matcher import FIELDS, OPTIONAL  # noqa: E402
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
    found = publics(args.pdb, list(FIELDS.values()))
    print('| Function | Expected | Actual | Match |')
    print('|---|---|---|---|')
    ok = True
    for field, name in FIELDS.items():
        actual = found.get(name)
        shown = f'0x{actual:X}' if actual is not None else 'not found'
        if field in OPTIONAL and not entry[field]:
            print(f'| {field} | not in the table | {shown} | - |')
            continue
        match = actual == entry[field]
        ok &= match
        print(f'| {field} | 0x{entry[field]:X} | {shown} | {"yes" if match else "no"} |')
    print()
    print('All match.' if ok else 'Some addresses differ: correct kKnownBuilds before more users rely on them.')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
