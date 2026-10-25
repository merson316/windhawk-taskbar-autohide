"""A line-for-line port of the mod's ScanForOffsets, so the matcher and the
"Should work on" check accept exactly the GetAutoHideFlags the mod does."""
import struct


def scan_for_offsets(fn, layouts):
    """(layout, edge, flags) if the function bytes `fn` (at least 0x100 of
    them) hold exactly one edge test in an accepted layout with plausible
    offsets, else None. layouts: {'old'} or {'old', 'inverted'}."""
    length = 0x100
    for i in range(0x100 - 3):
        if fn[i:i + 4] == b'\xCC\xCC\xCC\xCC':
            length = i
            break

    found = None
    matches = 0
    for i in range(length - 11):
        p = fn[i:i + 16]
        if p[0] != 0x83 or p[1] != 0xBB or p[6] != 0x03:
            continue
        if i + 16 <= length and p[7:15] == b'\x74\x04\x33\xC0\xEB\x03\x8B\x43':
            layout, flags = 'old', p[15]
        elif 'inverted' in layouts and p[7] == 0x75 and p[9] == 0x8B and p[10] == 0x43:
            zero = i + 9 + p[8]
            if p[8] >= 0x80 or zero + 2 > length or fn[zero:zero + 2] != b'\x33\xC0':
                continue
            layout, flags = 'inverted', p[11]
        else:
            continue
        found = (layout, struct.unpack_from('<I', p, 2)[0], flags)
        matches += 1

    if matches != 1:
        return None
    layout, edge, flags = found
    if edge < 8 or edge > 0x1000 or flags < 8:
        return None
    return found
