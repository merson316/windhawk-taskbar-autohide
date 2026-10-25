from release_notes import OTHER_BUILDS, render

COMPAT = [
    {'os_build': '26300.9807', 'taskbar': '10.0.26100.9807', 'support': 'built-in', 'tested_with': ['1.17.0']},
    {'os_build': '26300.9549', 'taskbar': '10.0.26100.9549', 'support': 'symbols', 'tested_with': ['1.15.0']},
]
KNOWN = [{'version': '10.0.26100.9807', 'pdb_key': '1AF71656D4C04FBD463BFF7E4ED169CE1'}]


def test_render_1_17_0():
    notes = render('1.17.0', COMPAT, KNOWN, ['Works on taskbar.dll 10.0.26100.9807 without its symbols.'])
    assert '## Works with' in notes
    assert '| 26300.9807 | 10.0.26100.9807 | built-in |' in notes
    assert '26300.9549' not in notes
    assert '- taskbar.dll 10.0.26100.9807' in notes
    assert OTHER_BUILDS in notes
    assert OTHER_BUILDS == ('Other builds: works wherever Windhawk can download the taskbar\'s symbols, '
                            'unless an update changes the code the mod checks; in that case the mod shows '
                            'a warning instead of hooking.')
    assert notes.index('## Works with') < notes.index('## Changes') < notes.index('## Install')
    assert '- Works on taskbar.dll 10.0.26100.9807 without its symbols.' in notes


def test_render_no_tested():
    notes = render('1.16.0', COMPAT, [], ['Says when symbols are not published yet.'])
    assert '| OS build |' not in notes
    assert 'Built-in support' not in notes
    assert OTHER_BUILDS in notes
    assert '## Changes' in notes and '## Install' in notes


def test_record_tested(tmp_path, dll_9807):
    import json
    from release_notes import record_tested
    path = tmp_path / 'compat.json'
    path.write_text('[]')
    record_tested(path, '26300.9807', dll_9807, 'src/taskbar-autohide-any-edge.wh.cpp', '1.17.0')
    record_tested(path, '26300.9807', dll_9807, 'src/taskbar-autohide-any-edge.wh.cpp', '1.18.0')
    assert json.loads(path.read_text()) == [{
        'os_build': '26300.9807', 'taskbar': '10.0.26100.9807', 'support': 'built-in',
        'tested_with': ['1.17.0', '1.18.0']}]
