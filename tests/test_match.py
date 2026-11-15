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


def test_candidate_off_function_start_rejected(monkeypatch, dll_9549, pdb_9549, dll_9807):
    # A build that shifts SlideWindow's body would move the duration loads with it;
    # the anchor would then point a few bytes into the function. Never emit that.
    from matcher import anchors
    monkeypatch.setattr(anchors, 'slide_window', lambda pe: [0x41263])
    result = match_build(PE(dll_9807), PE(dll_9549), fields(pdb_9549))
    assert result.rvas is None
    assert 'slideWindow' in result.reason


def test_slide_window_anchor_requires_function_start(dll_9807):
    from matcher import anchors
    assert anchors.slide_window(PE(dll_9807)) == [0x41260]


# ---- a function Microsoft reworked: the vtable slot as evidence -------------

EXPECTED_10066 = {
    'getStuckPlace': 0x27F20, 'getAutoHideFlags': 0x45020, 'setAutoHideFlags': 0xB0810,
    'getDockedRect': 0x44F80, 'unhide': 0x1A2B0, 'slideWindow': 0xB08B0,
    'permitAutoHide': 0x7F5D0,
}


def strict_rejects(monkeypatch, *names):
    """Make the similarity gate turn these functions down, as it does when
    Microsoft rewrites one."""
    from matcher import match
    real = match._accept

    def accept(fieldname, cand, ref):
        ok, sim, imports, offsets = real(fieldname, cand, ref)
        return ok and fieldname not in names, sim, imports, offsets
    monkeypatch.setattr(match, '_accept', accept)


def verdicts(result):
    return {r.field: r.verdict for r in result.rows}


def test_match_10066_reworked_functions(dll_10066, dll_9974, pdb_9974):
    # SlideWindow was rewritten (similarity 0.36) and SetAutoHideFlags gained a
    # member; both still sit in the vtable slots they had in 9974.
    result = match_build(PE(dll_10066), PE(dll_9974), fields(pdb_9974))
    assert result.reason is None
    assert result.rvas == EXPECTED_10066
    v = verdicts(result)
    assert v['slideWindow'] == v['setAutoHideFlags'] == 'ok (vtable slot)'
    assert v['unhide'] == v['getDockedRect'] == v['getStuckPlace'] == 'ok'


def test_slide_window_anchor_finds_moved_loads(dll_10066):
    from matcher import anchors
    assert 0xB08B0 in anchors.slide_window(PE(dll_10066))


def test_vtable_slot_accepts_a_reworked_function(monkeypatch, dll_9549, pdb_9549, dll_9807):
    strict_rejects(monkeypatch, 'slideWindow')
    result = match_build(PE(dll_9807), PE(dll_9549), fields(pdb_9549))
    assert result.reason is None
    assert result.rvas == EXPECTED_9807
    assert verdicts(result)['slideWindow'] == 'ok (vtable slot)'


def test_vtable_slot_needs_two_functions_matched_the_strict_way(monkeypatch, dll_9549, pdb_9549, dll_9807):
    # With only GetStuckPlace matched strictly there is nothing to line the
    # two vtables up against, so nothing may be accepted on its slot.
    strict_rejects(monkeypatch, 'setAutoHideFlags', 'unhide', 'getDockedRect', 'slideWindow')
    result = match_build(PE(dll_9807), PE(dll_9549), fields(pdb_9549))
    assert result.rvas is None
    assert 'slideWindow' in result.reason


def test_vtable_slot_rejects_a_function_in_another_slot(dll_9549, pdb_9549, dll_9807):
    from matcher.match import same_slot
    new, ref, r = PE(dll_9807), PE(dll_9549), fields(pdb_9549)
    pairs = [(r[f], EXPECTED_9807[f]) for f in ('getStuckPlace', 'getDockedRect', 'unhide')]
    assert same_slot(new, ref, EXPECTED_9807['slideWindow'], r['slideWindow'], pairs)
    # SetAutoHideFlags is a TrayUI virtual too, one slot before GetDockedRect.
    assert not same_slot(new, ref, EXPECTED_9807['setAutoHideFlags'], r['slideWindow'], pairs)
    # PermitAutoHide belongs to another class's vtable altogether.
    assert not same_slot(new, ref, EXPECTED_9807['permitAutoHide'], r['slideWindow'], pairs)
    assert not same_slot(new, ref, EXPECTED_9807['slideWindow'], r['slideWindow'], pairs[:1])


def test_vtable_slot_does_not_excuse_different_imports(monkeypatch, dll_9549, pdb_9549, dll_9807):
    # The slot says which virtual this is, not that it still does the same job.
    from matcher import match
    real = match._accept

    def accept(fieldname, cand, ref):
        ok, sim, imports, offsets = real(fieldname, cand, ref)
        return (False, sim, False, offsets) if fieldname == 'slideWindow' else (ok, sim, imports, offsets)
    monkeypatch.setattr(match, '_accept', accept)
    result = match_build(PE(dll_9807), PE(dll_9549), fields(pdb_9549))
    assert result.rvas is None
    assert verdicts(result)['slideWindow'] == 'rejected'
