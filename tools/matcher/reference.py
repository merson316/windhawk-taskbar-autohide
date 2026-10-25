"""Finding a reference build: an older taskbar.dll from WinSxS whose PDB is in
Windhawk's symbol cache, ours, or on Microsoft's symbol server."""
import glob
import os
import urllib.error
import urllib.request

from .match import pick_reference
from .pe import PE

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
