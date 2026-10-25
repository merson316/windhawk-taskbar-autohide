from release_notes import OTHER_BUILDS, WINVER_TIP, render, should_work_for

COMPAT = [
    {'os_build': '26300.9807', 'taskbar': '10.0.26100.9807', 'support': 'built-in', 'tested_with': ['1.17.0']},
    {'os_build': '26300.9549', 'taskbar': '10.0.26100.9549', 'support': 'symbols', 'tested_with': ['1.15.0']},
]
KNOWN = [{'version': '10.0.26100.9807', 'pdb_key': '1AF71656D4C04FBD463BFF7E4ED169CE1'}]


def test_render_1_17_0():
    notes = render('1.17.0', COMPAT, ['Works on taskbar.dll 10.0.26100.9807 without its symbols.'])
    assert '## Works with' in notes
    assert '| OS build (winver) | taskbar.dll | How the mod finds the taskbar |' in notes
    assert '| 26300.9807 | 10.0.26100.9807 | Built-in addresses (Microsoft has not published symbols) |' in notes
    assert WINVER_TIP in notes
    assert '26300.9549' not in notes
    assert OTHER_BUILDS in notes
    assert OTHER_BUILDS == ('Other builds: works wherever Windhawk can download the taskbar\'s symbols, '
                            'unless an update changes the code the mod checks; in that case the mod shows '
                            'a warning instead of hooking.')
    assert notes.index('## Works with') < notes.index('## Changes') < notes.index('## Install')
    assert '- Works on taskbar.dll 10.0.26100.9807 without its symbols.' in notes


def test_render_no_tested():
    notes = render('1.16.0', COMPAT, ['Says when symbols are not published yet.'])
    assert 'Tested on' not in notes
    assert 'Should work on' not in notes
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


BUILDS = [
    {'taskbar': f'10.0.26100.{rev}', 'symbols': True, 'resolved': True, 'layout': 'old', 'durations': True}
    for rev in (8521, 8655, 8737, 8875, 8972, 9168, 9278, 9444, 9549, 9711)
] + [
    {'taskbar': '10.0.26100.8457', 'symbols': True, 'resolved': True, 'layout': None, 'durations': True},
    {'taskbar': '10.0.26100.9999', 'symbols': True, 'resolved': True, 'layout': 'inverted', 'durations': True},
    {'taskbar': '10.0.26100.9998', 'symbols': False, 'resolved': None, 'layout': None, 'durations': False},
]


def test_should_work_for_respects_layouts_and_tested():
    tested = {'10.0.26100.9549'}
    old_only = [b['taskbar'] for b in should_work_for({'old'}, BUILDS, tested)]
    assert old_only[0] == '10.0.26100.9711' and '10.0.26100.9549' not in old_only
    assert '10.0.26100.9999' not in old_only and '10.0.26100.8457' not in old_only
    assert len(old_only) == 9
    both = [b['taskbar'] for b in should_work_for({'old', 'inverted'}, BUILDS, tested)]
    assert both[0] == '10.0.26100.9999' and '10.0.26100.9998' not in both


def test_render_should_work_folds_older_builds():
    sw = should_work_for({'old', 'inverted'}, BUILDS, {'10.0.26100.9807'})
    notes = render('1.17.0', COMPAT, ['x'], sw)
    assert '**Should work on**' in notes
    assert '| 26200.9999 / 26300.9999 | 10.0.26100.9999 |' in notes
    visible, folded = notes.split('<details>')
    assert visible.count('| 26200.') == 5
    assert '<summary>6 older builds</summary>' in folded
    assert notes.index('Should work on') < notes.index(OTHER_BUILDS) < notes.index('## Changes')


def test_built_in_builds_listed_when_not_retested():
    # 1.18.0 still carries 9807's addresses; a 9807 user must see that.
    sw = should_work_for({'old', 'inverted'}, BUILDS, set(), KNOWN)
    assert '10.0.26100.9807' in [b['taskbar'] for b in sw]
    notes = render('1.18.0', COMPAT, ['x'], sw)
    assert '| 26200.9807 / 26300.9807 | 10.0.26100.9807 | Built-in addresses |' in notes


def test_built_in_build_not_repeated_when_tested():
    sw = should_work_for({'old', 'inverted'}, BUILDS, {'10.0.26100.9807'}, KNOWN)
    assert '10.0.26100.9807' not in [b['taskbar'] for b in sw]
