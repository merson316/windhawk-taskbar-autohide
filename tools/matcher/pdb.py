"""Public-symbol lookup in a PDB without a PDB library: every public is an
S_PUB32 record that ends in its NUL-terminated mangled name."""
import struct

S_PUB32 = 0x110E


def publics(pdb_path, names):
    """Mangled name -> RVA for those of `names` that are public code symbols.

    Segment 1 is .text, which starts at RVA 0x1000 in taskbar.dll. A record
    split across two MSF pages is not found; the caller treats a missing name
    as a failure, never as a guess."""
    with open(pdb_path, 'rb') as f:
        data = f.read()
    out = {}
    for name in names:
        key = name.encode() + b'\0'
        i = data.find(key)
        while i >= 0:
            if i >= 12 and struct.unpack_from('<H', data, i - 12)[0] == S_PUB32:
                offset, segment = struct.unpack_from('<IH', data, i - 6)
                if segment == 1:
                    out[name] = 0x1000 + offset
                    break
            i = data.find(key, i + 1)
    return out
