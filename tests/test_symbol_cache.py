"""Windhawk keeps one symbol-cache entry per mod and PDB, holding the symbol
list of the last HookSymbols call on that module. A second call on taskbar.dll
overwrites the first call's entry, so every Explorer start misses the cache and
parses Taskbar.pdb again (issue #17). The mod must resolve everything it needs
from taskbar.dll in a single call."""
import os
import re

SRC = os.path.join(os.path.dirname(__file__), '..', 'src/taskbar-autohide-any-edge.wh.cpp')


def code_only(src):
    src = re.sub(r'(?s)/\*.*?\*/', '', src)
    return re.sub(r'//[^\n]*', '', src)


def test_taskbar_symbols_are_resolved_in_one_call():
    calls = re.findall(r'\bHookSymbols\s*\(', code_only(open(SRC).read()))
    assert len(calls) == 1, f'{len(calls)} HookSymbols calls; each one overwrites the cache entry of the one before'
