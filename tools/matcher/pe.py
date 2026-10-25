"""Just enough of the PE32+ format to identify a build, read its code and find
its functions, imports and strings. Reads the file as stored on disk."""
import bisect
import struct
import uuid

DIR_IMPORT, DIR_EXCEPTION, DIR_DEBUG, DIR_DELAY_IMPORT = 1, 3, 6, 13
UNW_FLAG_CHAININFO = 0x4


class PE:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self._load(f.read(), path)

    @classmethod
    def from_bytes(cls, data, name='<bytes>'):
        pe = cls.__new__(cls)
        pe._load(bytes(data), name)
        return pe

    def _load(self, data, name):
        self.data, self.name = data, name
        pe = struct.unpack_from('<I', data, 0x3C)[0]
        if data[pe:pe + 4] != b'PE\0\0':
            raise ValueError(f'{name}: not a PE file')
        nsec, self.timestamp = struct.unpack_from('<HI', data, pe + 6)
        opt_size = struct.unpack_from('<H', data, pe + 20)[0]
        opt = pe + 24
        if struct.unpack_from('<H', data, opt)[0] != 0x20B:
            raise ValueError(f'{name}: not PE32+')
        self.image_base = struct.unpack_from('<Q', data, opt + 24)[0]
        self.size_of_image = struct.unpack_from('<I', data, opt + 56)[0]
        ndirs = struct.unpack_from('<I', data, opt + 108)[0]
        self.dirs = [struct.unpack_from('<II', data, opt + 112 + 8 * i) for i in range(ndirs)]
        self.sections = []
        for i in range(nsec):
            o = opt + opt_size + 40 * i
            sname = data[o:o + 8].rstrip(b'\0').decode('ascii', 'replace')
            vsize, va, raw_size, raw_ptr = struct.unpack_from('<IIII', data, o + 8)
            chars = struct.unpack_from('<I', data, o + 36)[0]
            self.sections.append((sname, va, max(vsize, raw_size), raw_ptr, raw_size, chars))
        self._functions = None

    # -- addressing -------------------------------------------------------

    def _section(self, rva):
        for s in self.sections:
            if s[1] <= rva < s[1] + s[2]:
                return s
        return None

    def read(self, rva, n):
        s = self._section(rva)
        if s is None:
            raise ValueError(f'RVA {rva:#x} is outside every section')
        off = rva - s[1]
        out = self.data[s[3] + off:s[3] + min(off + n, s[4])]
        return out + b'\0' * (n - len(out))

    def u32(self, rva):
        return struct.unpack('<I', self.read(rva, 4))[0]

    def is_code(self, rva):
        s = self._section(rva)
        return bool(s and s[5] & 0x20000000)   # IMAGE_SCN_MEM_EXECUTE

    # -- identity ---------------------------------------------------------

    def codeview(self):
        rva, size = self.dirs[DIR_DEBUG]
        for i in range(size // 28):
            # IMAGE_DEBUG_DIRECTORY: Type, SizeOfData, AddressOfRawData, PointerToRawData
            typ, _, _, raw_ptr = struct.unpack('<IIII', self.read(rva + 28 * i + 12, 16))
            if typ == 2 and self.data[raw_ptr:raw_ptr + 4] == b'RSDS':
                guid = uuid.UUID(bytes_le=self.data[raw_ptr + 4:raw_ptr + 20])
                age = struct.unpack_from('<I', self.data, raw_ptr + 20)[0]
                return guid, age
        raise ValueError(f'{self.name}: no CodeView record')

    def pdb_key(self):
        guid, age = self.codeview()
        return f'{guid.hex.upper()}{age:X}'

    def dll_key(self):
        return f'{self.timestamp:08X}{self.size_of_image:x}'

    def file_version(self):
        sig = struct.pack('<I', 0xFEEF04BD)
        i = self.data.find(sig)
        if i < 0:
            raise ValueError(f'{self.name}: no version resource')
        ms, ls = struct.unpack_from('<II', self.data, i + 8)
        return f'{ms >> 16}.{ms & 0xFFFF}.{ls >> 16}.{ls & 0xFFFF}'

    # -- code -------------------------------------------------------------

    def _root(self, unwind):
        # A chained entry is a fragment of the function its chain ends at.
        for _ in range(32):
            hdr = self.read(unwind, 4)
            if not (hdr[0] >> 3) & UNW_FLAG_CHAININFO:
                return None
            codes = (hdr[2] + 1) & ~1
            begin, _, unwind = struct.unpack('<III', self.read(unwind + 4 + 2 * codes, 12))
            parent = begin
        return parent

    def _load_functions(self):
        rva, size = self.dirs[DIR_EXCEPTION]
        primary, fragments = [], []
        for i in range(size // 12):
            begin, end, unwind = struct.unpack('<III', self.read(rva + 12 * i, 12))
            root = self._root(unwind & ~1) if not unwind & 1 else None
            (fragments if root is not None else primary).append((begin, end, root))
        self._functions = sorted((b, e) for b, e, _ in primary)
        self._fragments = sorted(fragments)

    def functions(self):
        """(start, end) of every function with unwind data. Leaf functions,
        which need none, are absent; code.Function finds those by flow."""
        if self._functions is None:
            self._load_functions()
        return self._functions

    def function_containing(self, rva):
        """Start of the function whose unwind data covers rva, or None."""
        funcs = self.functions()
        i = bisect.bisect_right(funcs, (rva, 1 << 63)) - 1
        if i >= 0 and funcs[i][0] <= rva < funcs[i][1]:
            return funcs[i][0]
        for begin, end, root in self._fragments:
            if begin <= rva < end:
                return root
        return None

    def imports(self):
        """IAT slot RVA -> 'dll!name', for normal and delay-load imports."""
        if hasattr(self, '_imports'):
            return self._imports
        out = {}

        def thunks(dll, ilt, iat):
            for i in range(65536):
                entry = struct.unpack('<Q', self.read(ilt + 8 * i, 8))[0]
                if not entry:
                    break
                name = f'#{entry & 0xFFFF}' if entry >> 63 else self._cstr(entry + 2)
                out[iat + 8 * i] = f'{dll}!{name}'

        rva, size = self.dirs[DIR_IMPORT]
        for i in range(size // 20 if rva else 0):
            ilt, _, _, name, iat = struct.unpack('<IIIII', self.read(rva + 20 * i, 20))
            if not iat:
                break
            thunks(self._cstr(name).lower(), ilt or iat, iat)
        rva, size = self.dirs[DIR_DELAY_IMPORT] if len(self.dirs) > DIR_DELAY_IMPORT else (0, 0)
        for i in range(size // 32 if rva else 0):
            _, name, _, iat, ilt = struct.unpack('<IIIII', self.read(rva + 32 * i, 20))
            if not iat:
                break
            thunks(self._cstr(name).lower(), ilt, iat)
        self._imports = out
        return out

    def _cstr(self, rva):
        raw = self.read(rva, 512)
        return raw[:raw.index(b'\0')].decode('ascii', 'replace')

    def find_string(self, s, wide):
        """RVAs of s stored as a whole NUL-terminated string (not a suffix of
        a longer one)."""
        enc = s.encode('utf-16-le') if wide else s.encode('ascii')
        unit = 2 if wide else 1
        needle, out = enc + b'\0' * unit, []
        for sname, va, _, ptr, raw_size, _ in self.sections:
            blob = self.data[ptr:ptr + raw_size]
            i = blob.find(needle)
            while i >= 0:
                if i < unit or blob[i - unit:i] == b'\0' * unit:
                    out.append(va + i)
                i = blob.find(needle, i + unit)
        return out
