import re

import pytest

from matcher import HOOKED
from matcher.match import format_entry, match_build, pick_reference
from matcher.pdb import publics
from matcher.pe import PE

SRC = 'src/taskbar-autohide-any-edge.wh.cpp'
EXPECTED_9807 = {
    'getStuckPlace': 0x65250, 'getAutoHideFlags': 0x41F70, 'setAutoHideFlags': 0x3CC90,
    'getDockedRect': 0x41ED0, 'unhide': 0x5E670, 'slideWindow': 0x41260,
    'permitAutoHide': 0x35C40,
}


def fields(pdb):
    found = publics(pdb, list(HOOKED.values()))
    return {f: found[n] for f, n in HOOKED.items()}


def test_calibrate_9278_to_9549(dll_9278, pdb_9278, dll_9549, pdb_9549):
    result = match_build(PE(dll_9549), PE(dll_9278), fields(pdb_9278))
    assert result.reason is None
    assert result.rvas == fields(pdb_9549)


def test_reproduce_9807(dll_9549, pdb_9549, dll_9807):
    result = match_build(PE(dll_9807), PE(dll_9549), fields(pdb_9549))
    assert result.reason is None
    assert result.rvas == EXPECTED_9807


def test_format_entry_9807(dll_9807):
    src = open(SRC).read()
    entry = re.search(r'\{L"10\.0\.26100\.9807",.*?0x[0-9A-F]+\},\n', src, re.S).group(0)
    assert re.sub(r'\s', '', format_entry(PE(dll_9807), EXPECTED_9807)) == re.sub(r'\s', '', entry)


def test_fails_closed_missing_anchor(dll_9549, pdb_9549, dll_9807):
    pe = PE(dll_9807)
    data = bytearray(pe.data)
    needle = 'IsAutoHideEnabled'.encode('utf-16-le')
    i = data.find(needle)
    data[i:i + len(needle)] = bytes(len(needle))
    result = match_build(PE.from_bytes(data), PE(dll_9549), fields(pdb_9549))
    assert result.rvas is None
    assert 'setAutoHideFlags' in result.reason


def test_pick_reference_newest_below(dll_9278, dll_9549, dll_9807):
    cands = [(PE(dll_9278), 'a.pdb'), (PE(dll_9549), 'b.pdb'), (PE(dll_9807), 'c.pdb')]
    assert pick_reference(PE(dll_9807), cands)[1] == 'b.pdb'
    assert pick_reference(PE(dll_9549), cands)[1] == 'a.pdb'
    with pytest.raises(LookupError):
        pick_reference(PE(dll_9278), cands)
