import subprocess

from matcher import HOOKED
from matcher.buildcheck import accepted_layouts, check_build, os_builds
from matcher.pdb import publics
from matcher.pe import PE


def source_at(tag):
    return subprocess.run(['git', 'show', f'{tag}:src/taskbar-autohide-any-edge.wh.cpp'],
                          capture_output=True, text=True, check=True).stdout


def test_accepted_layouts():
    assert accepted_layouts(source_at('v1.15.0')) == {'old'}
    assert accepted_layouts(source_at('v1.16.0')) == {'old', 'inverted'}
    assert accepted_layouts(open('src/taskbar-autohide-any-edge.wh.cpp').read()) == {'old', 'inverted'}


def test_check_build_9549(dll_9549, pdb_9549):
    assert check_build(PE(dll_9549), pdb_9549) == {
        'taskbar': '10.0.26100.9549', 'resolved': True, 'layout': 'old', 'durations': True}


def test_check_build_inverted_layout(dll_9807, dll_9549, pdb_9549):
    # 9807 has no PDB; check its GetAutoHideFlags at the known address instead.
    rvas = {'getAutoHideFlags': 0x41F70, 'slideWindow': 0x41260}
    assert check_build(PE(dll_9807), None, rvas)['layout'] == 'inverted'


def test_os_builds():
    assert os_builds('10.0.26100.9711') == '26200.9711 / 26300.9711'


def test_winbindex_builds():
    from matcher.buildcheck import winbindex_builds
    data = {
        'a': {'fileInfo': {'machineType': 34404, 'version': '10.0.26100.9711 (WinBuild.160101.0800)',
                           'timestamp': 1989904251, 'virtualSize': 3178496}},
        'b': {'fileInfo': {'machineType': 332, 'version': '10.0.26100.9711', 'timestamp': 1, 'virtualSize': 2}},
        'c': {'fileInfo': {'machineType': 34404, 'version': '10.0.22631.5000', 'timestamp': 1, 'virtualSize': 2}},
        'd': {'fileInfo': {'machineType': 34404}},
        'e': {'fileInfo': {'machineType': 34404, 'version': '10.0.26100.9711', 'timestamp': 1989904251,
                           'virtualSize': 3178496}},
    }
    assert winbindex_builds(data) == [('10.0.26100.9711', 1989904251, 3178496)]
