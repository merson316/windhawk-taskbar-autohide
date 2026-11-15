"""matcher.scan must accept exactly what the mod's ScanForOffsets accepts."""
from matcher.pe import PE
from matcher.scan import scan_for_offsets

BOTH = {'old', 'inverted'}
OLD = bytes.fromhex('83BB1801000003 7404 33C0 EB03 8B4344')
INVERTED = bytes.fromhex('83BB1801000003 7509 8B4344 4883C4205BC3 33C0 EBF6')
PAD = b'\xCC' * 8


def test_real_builds(dll_9549, dll_9807):
    assert scan_for_offsets(PE(dll_9549).read(0x16500, 0x100), BOTH) == ('old', 0x118, 0x44)
    assert scan_for_offsets(PE(dll_9807).read(0x41F70, 0x100), BOTH) == ('inverted', 0x118, 0x44)
    assert scan_for_offsets(PE(dll_9807).read(0x41F70, 0x100), {'old'}) is None


def test_synthetic_layouts():
    assert scan_for_offsets(b'\x90' * 4 + OLD + PAD, BOTH) == ('old', 0x118, 0x44)
    assert scan_for_offsets(b'\x90' * 4 + INVERTED + PAD, BOTH) == ('inverted', 0x118, 0x44)


def test_two_matches_rejected():
    assert scan_for_offsets(OLD + OLD + PAD, BOTH) is None


def test_match_after_padding_ignored():
    assert scan_for_offsets(b'\x90' * 4 + PAD + OLD, BOTH) is None


def test_bounds():
    small_edge = OLD.replace(bytes.fromhex('18010000'), bytes.fromhex('04000000'))
    assert scan_for_offsets(small_edge + PAD, BOTH) is None
    small_flags = OLD[:-1] + b'\x04'
    assert scan_for_offsets(small_flags + PAD, BOTH) is None


def test_inverted_jump_must_land_on_xor():
    wrong_target = INVERTED.replace(bytes.fromhex('7509'), bytes.fromhex('7508'))
    assert scan_for_offsets(wrong_target + PAD, BOTH) is None


class FakePE:
    def __init__(self, fn):
        self.fn = fn

    def read(self, rva, n):
        return (self.fn + b'\xCC' * n)[:n]


def test_buildcheck_layout_follows_the_mod():
    # A jne that does not land on xor eax,eax: the mod refuses, so must we.
    from matcher.buildcheck import layout
    wrong_target = INVERTED.replace(bytes.fromhex('7509'), bytes.fromhex('7508'))
    assert layout(FakePE(wrong_target + PAD), 0) is None
    assert layout(FakePE(INVERTED + PAD), 0) == 'inverted'


def test_anchor_requires_the_mods_scan(monkeypatch, dll_9807):
    from matcher import anchors, scan
    assert anchors.get_auto_hide_flags(PE(dll_9807)) == [0x41F70]
    monkeypatch.setattr(scan, 'scan_for_offsets', lambda fn, layouts: None)
    assert anchors.get_auto_hide_flags(PE(dll_9807)) == []


# ---- SlideWindow's duration loads ------------------------------------------

from matcher.scan import scan_for_durations  # noqa: E402

SHOW_R14 = bytes.fromhex('448BB36C010000')   # mov r14d,[rbx+16Ch]
HIDE_R14 = bytes.fromhex('448BB368010000')   # mov r14d,[rbx+168h]
SHOW_R15 = bytes.fromhex('448BBB6C010000')   # mov r15d,[rbx+16Ch]
HIDE_R15 = bytes.fromhex('448BBB68010000')   # mov r15d,[rbx+168h]
NOPS = b'\x90' * 16


def test_durations_real_builds(dll_9549, pdb_9549, dll_9807):
    from matcher import HOOKED
    from matcher.pdb import publics
    slide = publics(pdb_9549, [HOOKED['slideWindow']])[HOOKED['slideWindow']]
    assert scan_for_durations(PE(dll_9549).read(slide, 0x200))
    assert scan_for_durations(PE(dll_9807).read(0x41260, 0x200))
    # GetDockedRect loads neither duration.
    assert not scan_for_durations(PE(dll_9807).read(0x41ED0, 0x200))


def test_durations_reworked_build(dll_10066):
    # 10066 moved both loads and changed their register.
    assert scan_for_durations(PE(dll_10066).read(0xB08B0, 0x200))


def test_durations_found_wherever_the_loads_sit():
    assert scan_for_durations(NOPS + SHOW_R14 + NOPS * 5 + HIDE_R14 + NOPS)
    assert scan_for_durations(NOPS * 9 + SHOW_R15 + b'\xEB\x07' + HIDE_R15 + NOPS)
    assert scan_for_durations(NOPS + HIDE_R15 + NOPS + SHOW_R15 + NOPS)


def test_durations_need_both_loads():
    assert not scan_for_durations(NOPS + SHOW_R14 + NOPS * 4)
    assert not scan_for_durations(NOPS + HIDE_R14 + NOPS * 4)


def test_durations_need_one_register():
    # One load into r14d and the other into r15d is not the one duration
    # variable SlideWindow fills on either branch.
    assert not scan_for_durations(NOPS + SHOW_R14 + NOPS + HIDE_R15 + NOPS)


def test_durations_repeated_load_rejected():
    assert not scan_for_durations(NOPS + SHOW_R14 + HIDE_R14 + NOPS + SHOW_R14 + NOPS)


def test_durations_other_base_register_rejected():
    # mov r14d,[rsi+16Ch]: the durations are read from this, which is in rbx.
    other = bytes.fromhex('448BB66C010000') + NOPS + bytes.fromhex('448BB668010000')
    assert not scan_for_durations(NOPS + other + NOPS)


def test_durations_beyond_the_scanned_bytes_ignored():
    assert not scan_for_durations(b'\x90' * 0x200 + SHOW_R14 + HIDE_R14)


def test_durations_after_the_functions_padding_ignored():
    # A short function must not be credited with the loads of the one after it.
    assert not scan_for_durations(NOPS + b'\xCC' * 8 + SHOW_R14 + NOPS + HIDE_R14 + NOPS)
