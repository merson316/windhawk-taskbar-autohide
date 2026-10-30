import re
import shutil
import subprocess
import sys

import pytest

from matcher import HOOKED
from matcher.apply import apply_entry, known_builds
from matcher.match import format_entry
from matcher.pdb import publics
from matcher.pe import PE

SRC, README = 'src/taskbar-autohide-any-edge.wh.cpp', 'README.md'
FAKE = ('    {L"10.0.26100.9999",\n'
        '     {0x11111111, 0x1111, 0x1111, {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11}},\n'
        '     1, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7},\n')


@pytest.fixture
def copies(tmp_path):
    src, readme = tmp_path / 'mod.wh.cpp', tmp_path / 'README.md'
    shutil.copy(SRC, src)
    shutil.copy(README, readme)
    return src, readme


def test_known_builds_parses_9807():
    (entry,) = known_builds(SRC)
    assert entry['version'] == '10.0.26100.9807'
    assert entry['pdb_key'] == '1AF71656D4C04FBD463BFF7E4ED169CE1'
    assert [entry[f] for f in HOOKED] == [0x65250, 0x41F70, 0x3CC90, 0x41ED0, 0x5E670, 0x41260, 0x35C40]


def next_minor():
    major, minor, _ = re.search(r'(?m)^// @version\s+(\S+)$', open(SRC).read()).group(1).split('.')
    return f'{major}.{int(minor) + 1}.0'


def test_apply_to_temp_copy(copies):
    src, readme = copies
    assert apply_entry(src, readme, FAKE, '10.0.26100.9999', '10.0.26100.9807') == next_minor()
    assert len(known_builds(src)) == 2
    assert f'@version         {next_minor()}' in src.read_text()
    assert '| 10.0.26100.9999 | 10.0.26100.9807 |' in readme.read_text()
    assert subprocess.run(['tools/compile-mod.sh', str(src)]).returncode == 0


def test_apply_duplicate_refused(copies):
    src, readme = copies
    entry = open(SRC).read()
    nine = entry[entry.index('    {L"10.0.26100.9807"'):]
    nine = nine[:nine.index('0x35C40},\n') + len('0x35C40},\n')]
    with pytest.raises(ValueError):
        apply_entry(src, readme, nine, '10.0.26100.9807', '10.0.26100.9549')


def test_verify_against_9549_pdb(copies, dll_9549, pdb_9549):
    src, readme = copies
    found = publics(pdb_9549, list(HOOKED.values()))
    rvas = {f: found[n] for f, n in HOOKED.items()}
    apply_entry(src, readme, format_entry(PE(dll_9549), rvas), '10.0.26100.9549', '10.0.26100.9278')
    cmd = [sys.executable, 'tools/verify-known-build.py', '--src', str(src), '--pdb', pdb_9549,
           '--version', '10.0.26100.9549']
    ok = subprocess.run(cmd, capture_output=True, text=True)
    assert ok.returncode == 0, ok.stdout + ok.stderr
    src.write_text(src.read_text().replace(f'0x{rvas["unhide"]:X},', '0x924C1,'))
    bad = subprocess.run(cmd, capture_output=True, text=True)
    assert bad.returncode == 1
    assert '| unhide | 0x924C1 | 0x924C0 | no |' in bad.stdout


def test_apply_to_empty_table(copies):
    src, readme = copies
    text = src.read_text()
    start = text.index('const KnownBuild kKnownBuilds[] = {\n') + len('const KnownBuild kKnownBuilds[] = {\n')
    src.write_text(text[:start] + text[text.index('};', start):])
    assert known_builds(src) == []
    apply_entry(src, readme, FAKE, '10.0.26100.9999', '10.0.26100.9807')
    assert [e['version'] for e in known_builds(src)] == ['10.0.26100.9999']
    assert 'kKnownBuilds[] = {\n    // Matched against' in src.read_text()
