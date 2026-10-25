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
