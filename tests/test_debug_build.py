import os
import subprocess
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from make_debug_build import debug_source, has_switch  # noqa: E402

SRC = os.path.join(ROOT, 'src', 'taskbar-autohide-any-edge.wh.cpp')


def source():
    with open(SRC, newline='') as f:
        return f.read()


def test_debug_build_differs_by_one_line():
    a, b = source().split('\n'), debug_source(source()).split('\n')
    assert len(a) == len(b)
    changed = [(x, y) for x, y in zip(a, b) if x != y]
    assert changed == [('// #define TASKBAR_AUTOHIDE_DEBUG', '#define TASKBAR_AUTOHIDE_DEBUG')]


def test_shipped_source_has_the_detailed_log_off():
    assert has_switch(source())
    assert '\n#define TASKBAR_AUTOHIDE_DEBUG\n' not in source()


def test_a_source_without_the_switch_is_refused():
    with pytest.raises(ValueError):
        debug_source('int main() {}\n')
    assert not has_switch('int main() {}\n')


def test_only_the_debug_build_logs_each_request():
    # The lines a debug build adds go through DLog, which is nothing in the
    # shipped build.
    text = source()
    assert '#define DLog(...) ((void)0)' in text
    assert 'DLog(L"taskbar %p: Windows asks' in text
    assert 'Wh_Log(L"taskbar %p: Windows asks' not in text


def test_debug_build_compiles(tmp_path):
    out = tmp_path / 'debug.wh.cpp'
    out.write_text(debug_source(source()), newline='')
    r = subprocess.run([os.path.join(ROOT, 'tools', 'compile-mod.sh'), str(out), str(tmp_path / 'debug.dll')],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
