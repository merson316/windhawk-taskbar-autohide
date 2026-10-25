"""Fixtures pointing at real taskbar.dll builds and PDBs on this PC. Tests skip
where a file is missing, so the suite runs anywhere but proves most on the
machine the builds were captured on."""
import glob
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools'))

WINSXS = '/mnt/c/Windows/WinSxS'
SYMBOLS = '/mnt/c/ProgramData/Windhawk/Engine/Symbols/Taskbar.pdb'


def _one(pattern):
    hits = glob.glob(pattern)
    if not hits:
        pytest.skip(f'not on this machine: {pattern}')
    return hits[0]


def _dll(ver):
    return _one(f'{WINSXS}/amd64_microsoft-windows-taskbar-dll_31bf3856ad364e35_10.0.26100.{ver}_none_*/Taskbar.dll')


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
    return _one(f'{SYMBOLS}/EB0D05FD4C8B8C721BFCBF0624FCD2881/Taskbar.pdb')


@pytest.fixture
def pdb_9549():
    return _one(f'{SYMBOLS}/0A5DD95BEF53208B28B2F3D8586E64261/Taskbar.pdb')
