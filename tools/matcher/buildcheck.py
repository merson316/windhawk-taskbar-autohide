"""Whether a taskbar build should work with a mod version: its symbols resolve,
and its code passes the checks the mod runs when it loads."""
import re

from . import HOOKED
from .pdb import publics

_OLD = re.compile(rb'\x83\xBB....\x03\x74\x04\x33\xC0\xEB\x03\x8B\x43.', re.S)
_INVERTED = re.compile(rb'\x83\xBB....\x03\x75.\x8B\x43.', re.S)
SHOW_DURATION = (0xA6, bytes.fromhex('448BB36C010000'))
HIDE_DURATION = (0x123, bytes.fromhex('448BB368010000'))


def accepted_layouts(src_text):
    """GetAutoHideFlags layouts a version's ScanForOffsets accepts, read from
    that version's source."""
    body = src_text[src_text.index('bool ScanForOffsets('):]
    body = body[:body.index('\n}\n')]
    return {'old', 'inverted'} if 'p[7] == 0x75' in body else {'old'}


def layout(pe, rva):
    fn = pe.read(rva, 0x100)
    pad = fn.find(b'\xCC' * 4)
    fn = fn[:pad if pad >= 0 else len(fn)]
    old, inverted = len(_OLD.findall(fn)), len(_INVERTED.findall(fn))
    if old + inverted != 1:
        return None
    return 'old' if old else 'inverted'


def check_build(pe, pdb_path, rvas=None):
    if rvas is None:
        found = publics(pdb_path, list(HOOKED.values()))
        rvas = {f: found[n] for f, n in HOOKED.items() if n in found}
    resolved = all(f in rvas for f in HOOKED) if pdb_path else None
    out = {'taskbar': pe.file_version(), 'resolved': resolved, 'layout': None, 'durations': False}
    if 'getAutoHideFlags' in rvas:
        out['layout'] = layout(pe, rvas['getAutoHideFlags'])
    if 'slideWindow' in rvas:
        out['durations'] = all(pe.read(rvas['slideWindow'] + off, len(sig)) == sig
                               for off, sig in (SHOW_DURATION, HIDE_DURATION))
    return out


def os_builds(taskbar_version):
    """The same taskbar.dll ships on 25H2 and 26H2; the mod needs 26200+."""
    rev = taskbar_version.rsplit('.', 1)[1]
    return f'26200.{rev} / 26300.{rev}'


def winbindex_builds(data):
    """(version, timestamp, size) of each x64 26100 taskbar.dll Winbindex knows,
    oldest first. Timestamp and size are the msdl key for the DLL itself."""
    out = set()
    for entry in data.values():
        fi = entry.get('fileInfo', {})
        version = (fi.get('version') or '').split(' ')[0]
        if fi.get('machineType') == 0x8664 and version.startswith('10.0.26100.') and fi.get('timestamp'):
            out.add((version, fi['timestamp'], fi['virtualSize']))
    return sorted(out, key=lambda b: int(b[0].rsplit('.', 1)[1]))
