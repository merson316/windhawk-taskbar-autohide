"""Observed 9549 -> 9807 scores (normalised-instruction similarity), which
set the matcher's thresholds:
  setAutoHideFlags 1.000, getDockedRect 1.000, getStuckPlace 1.000,
  slideWindow 0.940, permitAutoHide 0.931, unhide 0.908,
  getAutoHideFlags 0.800 (same logic, branch laid out the other way round),
  unhide vs the secondarytray.cpp function 0.319."""
from matcher.code import Function, similarity
from matcher.pe import PE


def test_identical_functions(dll_9549, dll_9807):
    a = Function(PE(dll_9549), 0x91910)
    b = Function(PE(dll_9807), 0x3CC90)
    assert similarity(a, b) == 1.0
    assert a.import_calls() == b.import_calls()
    assert any(name.endswith('!SetPropW') for name in a.import_calls())
    assert a.member_offsets() == b.member_offsets()


def test_leaf_function_extent(dll_9807):
    f = Function(PE(dll_9807), 0x65250)
    assert f.size == 18
    assert f.normalised()[-1] == 'ret'


def test_close_functions(dll_9549, dll_9807):
    old, new = PE(dll_9549), PE(dll_9807)
    assert similarity(Function(old, 0x5D760), Function(new, 0x41260)) >= 0.90
    assert similarity(Function(old, 0x924C0), Function(new, 0x5E670)) >= 0.88
    assert similarity(Function(old, 0x61900), Function(new, 0x35C40)) >= 0.90


def test_unrelated_functions(dll_9549, dll_9807):
    # 0xBAE8C also references AutoHideOnTaskbarShow but is secondarytray.cpp code.
    assert similarity(Function(PE(dll_9549), 0x924C0), Function(PE(dll_9807), 0xBAE8C)) < 0.5
