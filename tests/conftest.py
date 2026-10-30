"""Fixtures for real taskbar.dll builds and their PDBs. Each comes from this
PC's WinSxS or Windhawk's symbol cache when present, else from Microsoft's
symbol server into ~/.cache/taskbar-matcher. Without either the test skips,
unless TASKBAR_FIXTURES=required (as in CI), where a missing file fails."""
import glob
import os
import sys
import urllib.error

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools'))

from matcher import reference  # noqa: E402
from matcher.reference import fetch_dll, fetch_pdb  # noqa: E402

WINSXS = os.environ.get('WINSXS', '/mnt/c/Windows/WinSxS')

# version: (TimeDateStamp, SizeOfImage) -- the symbol server's key for the DLL.
DLLS = {9278: (0x32859388, 0x304000), 9549: (0xE8E4B3D1, 0x308000), 9807: (0x7BA9A532, 0x31A000)}
PDBS = {9278: 'EB0D05FD4C8B8C721BFCBF0624FCD2881', 9549: '0A5DD95BEF53208B28B2F3D8586E64261'}


def _missing(what):
    if os.environ.get('TASKBAR_FIXTURES') == 'required':
        pytest.fail(f'fixture unavailable: {what}')
    pytest.skip(f'not available: {what}')


# The real locations, captured before any test stubs them: a fixture resolved
# after a stub must still fetch the real file.
_REAL = {k: getattr(reference, k) for k in ('MSDL', 'SYMBOLS', 'CACHE')}


def _fetch(fetch, *key):
    stubbed = {k: getattr(reference, k) for k in _REAL}
    try:
        for k, v in _REAL.items():
            setattr(reference, k, v)
        return fetch(*key)
    except (urllib.error.URLError, OSError, ValueError) as e:
        _missing(f'{key}: {e}')
    finally:
        for k, v in stubbed.items():
            setattr(reference, k, v)


def _dll(ver):
    hits = glob.glob(f'{WINSXS}/amd64_microsoft-windows-taskbar-dll_31bf3856ad364e35_10.0.26100.{ver}_none_*/Taskbar.dll')
    return hits[0] if hits else _fetch(fetch_dll, *DLLS[ver]) or _missing(f'taskbar.dll {ver}')


def _pdb(ver):
    return _fetch(fetch_pdb, PDBS[ver]) or _missing(f'Taskbar.pdb {ver}')


@pytest.fixture
def dll_9278():
    return _dll(9278)


@pytest.fixture
def dll_9549():
    return _dll(9549)


@pytest.fixture
def dll_9807():
    return _dll(9807)


@pytest.fixture
def pdb_9278():
    return _pdb(9278)


@pytest.fixture
def pdb_9549():
    return _pdb(9549)
