#!/usr/bin/env python3
"""Refresh docs/builds.json: every x64 26100 taskbar.dll Winbindex lists, with
whether its symbols are published and whether its code passes the mod's
load-time checks. Release notes read it for "Should work on".

  tools/check-builds.py"""
import gzip
import json
import os
import sys
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from matcher.buildcheck import check_build, winbindex_builds  # noqa: E402
from matcher.pe import PE  # noqa: E402
from matcher.reference import CACHE, MSDL, fetch_pdb  # noqa: E402

WINBINDEX = 'https://winbindex.m417z.com/data/by_filename_compressed/taskbar.dll.json.gz'
OUT = 'docs/builds.json'
UA = {'User-Agent': 'Microsoft-Symbol-Server/10.0.0.0'}


def fetch_dll(timestamp, size):
    key = f'{timestamp:08X}{size:x}'
    path = f'{CACHE}/dll/{key}/taskbar.dll'
    if not os.path.exists(path):
        req = urllib.request.Request(f'{MSDL}/taskbar.dll/{key}/taskbar.dll', headers=UA)
        try:
            data = urllib.request.urlopen(req, timeout=120).read()
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
            raise
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(data)
    return path


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
