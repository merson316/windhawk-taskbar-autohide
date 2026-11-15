import re

import pytest

from matcher import FIELDS, HOOKED, OPTIONAL
from matcher.match import format_entry, match_build, pick_reference
from matcher.pdb import publics
from matcher.pe import PE

SRC = 'src/taskbar-autohide-any-edge.wh.cpp'
EXPECTED_9807 = {
    'getStuckPlace': 0x65250, 'getAutoHideFlags': 0x41F70, 'setAutoHideFlags': 0x3CC90,
    'getDockedRect': 0x41ED0, 'unhide': 0x5E670, 'slideWindow': 0x41260,
    'permitAutoHide': 0x35C40, 'secondaryTrayUnhide': 0,
}


def fields(pdb):
    """The required functions only: a reference that says nothing of the
    optional one, which the matcher must then record as absent (0)."""
    found = publics(pdb, list(HOOKED.values()))
    return {f: found[n] for f, n in HOOKED.items()}


def all_fields(pdb):
    found = publics(pdb, list(FIELDS.values()))
    return {f: found[n] for f, n in FIELDS.items()}


def without_optional(rvas):
    return {**rvas, **dict.fromkeys(OPTIONAL, 0)}


def test_calibrate_9278_to_9549(dll_9278, pdb_9278, dll_9549, pdb_9549):
    result = match_build(PE(dll_9549), PE(dll_9278), fields(pdb_9278))
    assert result.reason is None
    assert result.rvas == without_optional(fields(pdb_9549))


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
    'permitAutoHide': 0x7F5D0, 'secondaryTrayUnhide': 0x15BBCC,
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
    result = match_build(PE(dll_10066), PE(dll_9974), all_fields(pdb_9974))
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


# ---- the optional function: CSecondaryTray::_Unhide -------------------------

SECONDARY = 'secondaryTrayUnhide'
HANDLE_IMMERSIVE_INVOKE = '?_HandleImmersiveInvoke@CSecondaryTray@@AEAA_NW4ImmersiveInvokeFlags@ImmersiveTray@@@Z'
HANDLE_IMMERSIVE_INVOKE_10066 = 0x15B764   # docs/taskbar-internals-outside.md


def test_optional_is_not_among_the_required():
    assert list(OPTIONAL) == [SECONDARY]
    assert SECONDARY not in HOOKED
    assert list(FIELDS) == list(HOOKED) + [SECONDARY]


def test_secondary_tray_unhide_calibrated_against_pdbs(dll_9278, pdb_9278, dll_9549, pdb_9549):
    result = match_build(PE(dll_9549), PE(dll_9278), all_fields(pdb_9278))
    assert result.reason is None
    assert result.rvas == all_fields(pdb_9549)
    assert verdicts(result)[SECONDARY] == 'ok'


def test_secondary_tray_unhide_10066(dll_10066, dll_9974, pdb_9974):
    from matcher import anchors
    from matcher.code import Function, similarity
    new, ref, ref_rvas = PE(dll_10066), PE(dll_9974), all_fields(pdb_9974)
    result = match_build(new, ref, ref_rvas)
    assert result.reason is None
    (row,) = [r for r in result.rows if r.field == SECONDARY]
    assert (row.verdict, row.rva, row.imports_equal, row.offsets_equal) == ('ok', 0x15BBCC, True, True)
    assert row.similarity >= 0.9
    assert result.rvas[SECONDARY] == 0x15BBCC != result.rvas['unhide']
    # It is the function _HandleImmersiveInvoke calls, in the reference (by
    # its symbols) and in 10066 (where that caller is the reference's, moved).
    ref_caller = publics(pdb_9974, [HANDLE_IMMERSIVE_INVOKE])[HANDLE_IMMERSIVE_INVOKE]
    assert ref_rvas[SECONDARY] in Function(ref, ref_caller).calls()
    new_caller = Function(new, HANDLE_IMMERSIVE_INVOKE_10066)
    assert similarity(new_caller, Function(ref, ref_caller)) >= 0.95
    assert 0x15BBCC in new_caller.calls()
    assert HANDLE_IMMERSIVE_INVOKE_10066 in anchors.callers(new, 0x15BBCC)


def test_optional_function_unmatched_leaves_the_build_accepted(monkeypatch, dll_10066, dll_9974, pdb_9974):
    strict_rejects(monkeypatch, SECONDARY)
    result = match_build(PE(dll_10066), PE(dll_9974), all_fields(pdb_9974))
    assert result.reason is None
    assert result.rvas == {**EXPECTED_10066, SECONDARY: 0}
    assert verdicts(result)[SECONDARY] == 'absent: rejected'


def test_optional_function_gets_no_vtable_slot_excuse(monkeypatch, dll_9549, pdb_9549, dll_9807):
    # It is not a virtual: only the strict way may accept it.
    strict_rejects(monkeypatch, SECONDARY, 'slideWindow')
    result = match_build(PE(dll_9807), PE(dll_9549), all_fields(pdb_9549))
    assert result.rvas == EXPECTED_9807
    assert verdicts(result)['slideWindow'] == 'ok (vtable slot)'
    assert verdicts(result)[SECONDARY] == 'absent: rejected'


def test_optional_function_off_function_start_is_absent(monkeypatch, dll_10066, dll_9974, pdb_9974):
    from matcher import anchors
    monkeypatch.setattr(anchors, 'secondary_tray_unhide', lambda pe: [0x15BBCC + 5])
    result = match_build(PE(dll_10066), PE(dll_9974), all_fields(pdb_9974))
    assert result.rvas == {**EXPECTED_10066, SECONDARY: 0}
    assert verdicts(result)[SECONDARY] == 'absent: 0x15bbd1 is not a function start'


def test_optional_function_not_in_the_reference_is_absent(dll_10066, dll_9974, pdb_9974):
    result = match_build(PE(dll_10066), PE(dll_9974), fields(pdb_9974))
    assert result.rvas == {**EXPECTED_10066, SECONDARY: 0}
    assert verdicts(result)[SECONDARY] == 'absent: not in the reference'


def test_required_function_still_fails_closed_with_the_optional_found(monkeypatch, dll_10066, dll_9974, pdb_9974):
    strict_rejects(monkeypatch, 'unhide')
    from matcher import match
    monkeypatch.setattr(match, 'same_slot', lambda *a: False)
    result = match_build(PE(dll_10066), PE(dll_9974), all_fields(pdb_9974))
    assert result.rvas is None
    assert 'unhide' in result.reason and SECONDARY not in result.reason


def test_format_entry_10066(dll_10066):
    src = open(SRC).read()
    entry = re.search(r'\{L"10\.0\.26100\.10066",.*?0x[0-9A-F]+\},\n', src, re.S).group(0)
    assert entry.rstrip().endswith('0x7F5D0, 0x15BBCC},')
    assert re.sub(r'\s', '', format_entry(PE(dll_10066), EXPECTED_10066)) == re.sub(r'\s', '', entry)


def test_format_entry_leaves_an_absent_optional_address_out(dll_10066):
    # C++ zero-fills the rest of the row, and rows written before the column
    # existed stay as they are.
    pe = PE(dll_10066)
    absent = format_entry(pe, {**EXPECTED_10066, SECONDARY: 0})
    assert absent.rstrip().endswith('0xB08B0, 0x7F5D0},')
    assert absent == format_entry(pe, {f: EXPECTED_10066[f] for f in HOOKED})
    assert format_entry(pe, EXPECTED_10066).rstrip().endswith('0x7F5D0, 0x15BBCC},')
