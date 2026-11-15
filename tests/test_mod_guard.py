"""The mod's KnownBuildLooksRight, compiled with Windhawk's own compiler and run
against a real taskbar.dll: the table's addresses pass, and moving any one of
them off its function start makes the whole entry fail."""
import os
import re
import subprocess

import pytest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SRC = os.path.join(REPO, 'src/taskbar-autohide-any-edge.wh.cpp')
WINDHAWK = os.environ.get('WINDHAWK_DIR', '/mnt/c/Program Files/Windhawk')
CLANG = os.path.join(WINDHAWK, 'Compiler/bin/clang++.exe')
FIELDS = ['getStuckPlace', 'getAutoHideFlags', 'setAutoHideFlags', 'getDockedRect',
          'unhide', 'slideWindow', 'permitAutoHide']

HARNESS = r'''
#include <windows.h>
#include <stdio.h>
%s

int main(int argc, char** argv) {
    HMODULE m = LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!m) return 2;
    KnownBuild b = kKnownBuilds[0];
    printf("table %%d\n", KnownBuildLooksRight(m, b));
    DWORD KnownBuild::*fields[] = {%s};
    for (int i = 0; i < %d; i++) {
        KnownBuild moved = b;
        moved.*fields[i] += 0x10;
        printf("moved %%d %%d\n", i, KnownBuildLooksRight(m, moved));
    }
    KnownBuild outside = b;
    outside.getStuckPlace = 0x7FFFFFF0;
    printf("outside %%d\n", KnownBuildLooksRight(m, outside));
    return 0;
}
'''


def extract(src):
    parts = [re.search(r'(?ms)^struct KnownBuild \{.*?^\};', src),
             re.search(r'(?ms)^const KnownBuild kKnownBuilds\[\] = \{.*?^\};', src),
             re.search(r'(?ms)^bool KnownBuildLooksRight\(.*?^\}', src)]
    assert all(parts), 'KnownBuild, kKnownBuilds or KnownBuildLooksRight not found in the mod'
    return '\n\n'.join(p.group(0) for p in parts)


def build(tmp_path, code):
    """Compile code with Windhawk's compiler; return a runner taking the
    harness's arguments, the first of them a DLL path."""
    if not os.path.exists(CLANG):
        if 'WINDHAWK_DIR' in os.environ:   # named explicitly, as in CI: must be there
            pytest.fail(f'no compiler at {CLANG}')
        pytest.skip("Windhawk's compiler is not installed")
    (tmp_path / 'harness.cpp').write_text(code)
    exe = tmp_path / 'harness.exe'
    if os.name == 'nt':   # CI runs this on Windows itself
        win = str
    else:
        win = lambda p: subprocess.run(['wslpath', '-w', str(p)], capture_output=True, text=True).stdout.strip()
    p = subprocess.run([CLANG, '-std=c++23', '-O2', '-target', 'x86_64-w64-mingw32',
                        win(tmp_path / 'harness.cpp'), '-o', win(exe)],
                       capture_output=True, text=True, cwd=os.path.dirname(os.path.dirname(CLANG)))
    assert p.returncode == 0, p.stderr
    exe.chmod(0o755)   # written from Windows, so it lands without the execute bit
    return lambda dll, *args: subprocess.run([str(exe), win(dll), *args], capture_output=True, text=True)


@pytest.fixture
def harness(tmp_path):
    return build(tmp_path, HARNESS % (extract(open(SRC).read()),
                                      ', '.join(f'&KnownBuild::{f}' for f in FIELDS), len(FIELDS)))


def test_table_addresses_pass_and_any_moved_one_fails(harness, dll_9807):
    p = harness(dll_9807)
    assert p.returncode == 0, p.stderr
    out = p.stdout.splitlines()
    assert out[0] == 'table 1'
    assert out[1:1 + len(FIELDS)] == [f'moved {i} 0' for i in range(len(FIELDS))]
    assert out[-1] == 'outside 0'


# ---- ScanForDurationLoads ---------------------------------------------------

DURATION_HARNESS = r'''
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
%s

int main(int argc, char** argv) {
    HMODULE m = LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!m) return 2;
    for (int i = 2; i < argc; i++) {
        DWORD rva = strtoul(argv[i], nullptr, 16);
        printf("%%lX %%d\n", rva, ScanForDurationLoads((const BYTE*)m + rva));
    }
    return 0;
}
'''


def extract_duration_scan(src):
    parts = [re.search(r'(?m)^#define SLIDE_HIDE_DURATION_OFFSET .*$', src),
             re.search(r'(?m)^#define SLIDE_SHOW_DURATION_OFFSET .*$', src),
             re.search(r'(?ms)^bool ScanForDurationLoads\(.*?^\}', src)]
    assert all(parts), 'the duration offsets or ScanForDurationLoads not found in the mod'
    return '\n\n'.join(p.group(0) for p in parts)


@pytest.fixture
def duration_scan(tmp_path):
    run = build(tmp_path, DURATION_HARNESS % extract_duration_scan(open(SRC).read()))

    def scan(dll, rvas):
        p = run(dll, *(f'{r:X}' for r in rvas))
        assert p.returncode == 0, p.stderr
        return {int(a, 16): b == '1' for a, b in (line.split() for line in p.stdout.splitlines())}
    return scan


def python_scan(dll, rvas):
    from matcher.pe import PE
    from matcher.scan import SLIDE_SCAN_LENGTH, scan_for_durations
    pe = PE(dll)
    return {r: scan_for_durations(pe.read(r, SLIDE_SCAN_LENGTH)) for r in rvas}


def test_duration_scan_accepts_slide_window_only(duration_scan, dll_9807):
    rvas = [0x65250, 0x41F70, 0x3CC90, 0x41ED0, 0x5E670, 0x41260, 0x35C40]
    got = duration_scan(dll_9807, rvas)
    assert got == {r: r == 0x41260 for r in rvas}
    assert got == python_scan(dll_9807, rvas), "the matcher's port disagrees with the mod"


def test_duration_scan_accepts_the_reworked_slide_window(duration_scan, dll_10066):
    rvas = [0x27F20, 0x45020, 0xB0810, 0x44F80, 0x1A2B0, 0xB08B0, 0x7F5D0]
    got = duration_scan(dll_10066, rvas)
    assert got == {r: r == 0xB08B0 for r in rvas}
    assert got == python_scan(dll_10066, rvas), "the matcher's port disagrees with the mod"
