from matcher import HOOKED
from matcher.pdb import publics
from matcher.pe import PE


def by_field(found):
    return {field: found[name] for field, name in HOOKED.items() if name in found}


def test_codeview_9807(dll_9807):
    pe = PE(dll_9807)
    assert pe.pdb_key() == '1AF71656D4C04FBD463BFF7E4ED169CE1'
    assert pe.dll_key() == '7BA9A53231a000'
    assert pe.file_version() == '10.0.26100.9807'


def test_publics_9549(pdb_9549):
    assert by_field(publics(pdb_9549, list(HOOKED.values()))) == {
        'getAutoHideFlags': 0x16500, 'setAutoHideFlags': 0x91910,
        'slideWindow': 0x5D760, 'getStuckPlace': 0x6B5E0,
        'getDockedRect': 0x16460, 'unhide': 0x924C0,
        'permitAutoHide': 0x61900,
    }


def test_publics_9278(pdb_9278):
    got = by_field(publics(pdb_9278, list(HOOKED.values())))
    assert got['getAutoHideFlags'] == 91104
    assert got['setAutoHideFlags'] == 599328
    assert got['slideWindow'] == 389216
    assert got['getStuckPlace'] == 445984
    assert got['getDockedRect'] == 90944
    assert got['permitAutoHide'] == 406544


def test_functions_contains(dll_9807):
    ranges = {start: end - start for start, end in PE(dll_9807).functions()}
    assert ranges.get(0x3CC90) == 176


def test_find_string(dll_9807):
    # SetAutoHideFlags passes it to SetPropW, so it is a UTF-16 literal.
    assert len(PE(dll_9807).find_string('IsAutoHideEnabled', wide=True)) == 1
