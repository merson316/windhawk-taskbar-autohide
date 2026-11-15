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


SLIDE_SCAN_LENGTH = 0x200
SHOW_DURATION_OFFSET = 0x16C
HIDE_DURATION_OFFSET = 0x168


def scan_for_durations(fn):
    """A line-for-line port of the mod's ScanForDurationLoads: True if the
    first 0x200 bytes of `fn` load this+0x16C and this+0x168 exactly once
    each, as `mov r32,[rbx+disp32]`, into the same register, before the
    padding that ends the function. Where the loads sit and which register
    takes them differ between builds."""
    length = min(len(fn), SLIDE_SCAN_LENGTH)
    for i in range(length - 3):
        if fn[i:i + 4] == b'\xCC\xCC\xCC\xCC':
            length = i
            break

    show = hide = None
    shows = hides = 0
    for i in range(length - 5):
        # 8B /r with mod=10, rm=011: mov r32,[rbx+disp32].
        if fn[i] != 0x8B or fn[i + 1] & 0xC7 != 0x83:
            continue
        disp = struct.unpack_from('<I', fn, i + 2)[0]
        reg = (fn[i + 1] >> 3) & 7
        if i > 0 and fn[i - 1] == 0x44:   # REX.R: r8d-r15d
            reg += 8
        if disp == SHOW_DURATION_OFFSET:
            show = reg
            shows += 1
        elif disp == HIDE_DURATION_OFFSET:
            hide = reg
            hides += 1
    return shows == 1 and hides == 1 and show == hide
