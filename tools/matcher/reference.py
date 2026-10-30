"""Finding a reference build: an older taskbar.dll from WinSxS whose PDB is in
Windhawk's symbol cache, ours, or on Microsoft's symbol server."""
import glob
import os
import struct
import urllib.error
import urllib.request

from .match import pick_reference
from .pe import PE

WINSXS = os.environ.get('WINSXS', '/mnt/c/Windows/WinSxS')
SYMBOLS = os.environ.get('WINDHAWK_SYMBOLS', '/mnt/c/ProgramData/Windhawk/Engine/Symbols/Taskbar.pdb')
MSDL = os.environ.get('MSDL_BASE', 'https://msdl.microsoft.com/download/symbols')
CACHE = os.environ.get('TASKBAR_CACHE', os.path.expanduser('~/.cache/taskbar-matcher'))


PDB_MAGIC = b'Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0'
_UA = {'User-Agent': 'Microsoft-Symbol-Server/10.0.0.0'}


def _is_pdb(path):
    with open(path, 'rb') as f:
        return f.read(len(PDB_MAGIC)) == PDB_MAGIC


def _is_pe(path):
    try:
        PE(path).pdb_key()
        return True
    except (OSError, ValueError, struct.error):
        return False


def _download(url, dest, valid, what):
    """Fetch url into dest only if the result passes `valid`; a partial or
    error-page response never reaches the cache. None on 404."""
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers=_UA), timeout=120) as r:
            data = r.read()
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    tmp = dest + '.part'
    with open(tmp, 'wb') as f:
        f.write(data)
    if not valid(tmp):
        os.unlink(tmp)
        raise ValueError(f'{url}: not a {what} ({len(data)} bytes)')
    os.replace(tmp, dest)
    return dest


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def pdb_published(key):
    """(published, HTTP status) for this PDB key on the symbol server. Only a
    404 means not published; callers treat other errors as unknown."""
    req = urllib.request.Request(f'{MSDL}/Taskbar.pdb/{key}/Taskbar.pdb', method='HEAD', headers=_UA)
    try:
        status = urllib.request.build_opener(_NoRedirect).open(req, timeout=30).status
    except urllib.error.HTTPError as e:
        status = e.code
    return status in (200, 302), status


def fetch_pdb(key):
    """A PDB for this key from Windhawk's cache, ours, or msdl; None if unpublished."""
    for path in (f'{SYMBOLS}/{key}/Taskbar.pdb', f'{CACHE}/{key}/Taskbar.pdb'):
        if os.path.exists(path) and _is_pdb(path):
            return path
    return _download(f'{MSDL}/Taskbar.pdb/{key}/Taskbar.pdb', f'{CACHE}/{key}/Taskbar.pdb', _is_pdb, 'PDB')


def fetch_dll(timestamp, size):
    """A taskbar.dll by its msdl key (timestamp + image size); None if absent."""
    key = f'{timestamp:08X}{size:x}'
    path = f'{CACHE}/dll/{key}/taskbar.dll'
    if os.path.exists(path) and _is_pe(path):
        return path
    return _download(f'{MSDL}/taskbar.dll/{key}/taskbar.dll', path, _is_pe, 'PE file')


def auto_reference(new):
    cands = []
    for dll in glob.glob(f'{WINSXS}/amd64_microsoft-windows-taskbar-dll_*/Taskbar.dll'):
        try:
            pe = PE(dll)
            pe.pdb_key()
        except (OSError, ValueError, struct.error):
            continue   # one unreadable copy must not stop the search
        if pe.pdb_key() == new.pdb_key():
            continue
        pdb = fetch_pdb(pe.pdb_key())
        if pdb:
            cands.append((pe, pdb))
    return pick_reference(new, cands)
