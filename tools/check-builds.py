#!/usr/bin/env python3
"""Refresh docs/builds.json: every x64 26100 taskbar.dll Winbindex lists, with
whether its symbols are published and whether its code passes the mod's
load-time checks. Release notes read it for "Should work on".

  tools/check-builds.py"""
import gzip
import json
import os
import sys
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from matcher.buildcheck import check_build, winbindex_builds  # noqa: E402
from matcher.pe import PE  # noqa: E402
from matcher.reference import fetch_dll, fetch_pdb  # noqa: E402

WINBINDEX = 'https://winbindex.m417z.com/data/by_filename_compressed/taskbar.dll.json.gz'
OUT = 'docs/builds.json'


def main():
    with urllib.request.urlopen(WINBINDEX, timeout=120) as r:
        index = json.loads(gzip.decompress(r.read()))
    out = []
    for version, timestamp, size in winbindex_builds(index):
        dll = fetch_dll(timestamp, size)
        if dll is None:
            print(f'{version}: DLL not on the symbol server, skipped')
            continue
        pe = PE(dll)
        pdb = fetch_pdb(pe.pdb_key())
        row = check_build(pe, pdb) if pdb else {'taskbar': version, 'resolved': None, 'layout': None,
                                                'durations': False}
        row = {'taskbar': row['taskbar'], 'symbols': pdb is not None, **{k: row[k] for k in
                                                                         ('resolved', 'layout', 'durations')}}
        out.append(row)
        print(f'{version}: {row}')
    with open(OUT, 'w') as f:
        json.dump(out, f, indent=2)
        f.write('\n')


if __name__ == '__main__':
    main()
