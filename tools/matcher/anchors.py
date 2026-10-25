"""Where to look for each hooked function in a build without symbols. Each
anchor returns candidate function starts; match.py decides which, if any, is
the function, by comparing with the reference build."""
import re
import struct

from . import scan

GET_STUCK_PLACE = bytes.fromhex('8B811C010000 83F8FF 7506 8B8110010000 C3')
SHOW_DURATION = bytes.fromhex('448BB36C010000')   # mov r14d,[rbx+16Ch], at +0xA6
HIDE_DURATION = bytes.fromhex('448BB368010000')   # mov r14d,[rbx+168h], at +0x123
PERMIT_FIELDS = bytes.fromhex('4C8D8790010000488D9798010000')   # lea r8,[rdi+190h]; lea rdx,[rdi+198h]
PERMIT_LIST = bytes.fromhex('488D8F40020000')   # lea rcx,[rdi+240h]


def _code(pe):
    for name, va, _, ptr, raw_size, chars in pe.sections:
        if chars & 0x20000000:
            yield va, pe.data[ptr:ptr + raw_size]


def find_bytes(pe, needle):
    out = []
    for va, blob in _code(pe):
        i = blob.find(needle)
        while i >= 0:
            out.append(va + i)
            i = blob.find(needle, i + 1)
    return out


def _rel32_sites(pe, target):
    """RVAs of every rel32 field that resolves to target, assuming the
    instruction ends with it (true for call rel32 and for RIP-relative
    lea/mov without an immediate)."""
    out = []
    for va, blob in _code(pe):
        for k in range(4):
            want = target - (va + k + 4)
            for j, (disp,) in enumerate(struct.iter_unpack('<i', blob[k:len(blob) - (len(blob) - k) % 4])):
                if disp + 4 * j == want:
                    out.append(va + k + 4 * j)
    return out


def _starts(pe, sites):
    return sorted({s for s in (pe.function_containing(r) for r in sites) if s is not None})


def callers(pe, target):
    return _starts(pe, [r for r in _rel32_sites(pe, target) if pe.read(r - 1, 1) == b'\xE8'])


def rip_referrers(pe, target):
    # ModRM with mod=00, rm=101 is [rip+disp32].
    return _starts(pe, [r for r in _rel32_sites(pe, target) if pe.read(r - 1, 1)[0] & 0xC7 == 0x05])


def string_referrers(pe, s):
    rvas = pe.find_string(s, wide=True) or pe.find_string(s, wide=False)
    return sorted({f for rva in rvas for f in rip_referrers(pe, rva)})


def get_auto_hide_flags(pe):
    """Functions holding the edge test the mod's ScanForOffsets accepts, in
    either branch layout."""
    shape = re.compile(rb'\x83\xBB....\x03(?:\x74\x04\x33\xC0\xEB\x03\x8B\x43.|\x75(.)\x8B\x43.)', re.S)
    sites = []
    for va, blob in _code(pe):
        for m in shape.finditer(blob):
            if m.group(1) is not None:
                zero = m.start() + 9 + m.group(1)[0]
                if m.group(1)[0] >= 0x80 or blob[zero:zero + 2] != b'\x33\xC0':
                    continue
            sites.append(va + m.start())
    # The regex only finds where to look; accept a function only if the
    # mod's own ScanForOffsets would accept it.
    return [f for f in _starts(pe, sites)
            if scan.scan_for_offsets(pe.read(f, 0x100), {'old', 'inverted'}) is not None]


def get_stuck_place(pe):
    return [r for r in find_bytes(pe, GET_STUCK_PLACE) if pe.read(r - 1, 1) == b'\xCC']


def slide_window(pe):
    # The loads sit at fixed offsets from the function start; a build that
    # shifts the body shifts them too, so require the start itself.
    return [r - 0xA6 for r in find_bytes(pe, SHOW_DURATION)
            if pe.function_containing(r) == r - 0xA6
            and pe.read(r - 0xA6 + 0x123, len(HIDE_DURATION)) == HIDE_DURATION]


def permit_auto_hide(pe):
    return sorted(set(_starts(pe, find_bytes(pe, PERMIT_FIELDS))) &
                  set(_starts(pe, find_bytes(pe, PERMIT_LIST))))
