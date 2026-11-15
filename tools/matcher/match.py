"""Decides, for each hooked function, which anchor candidate in the new build
is the reference build's function, and fails closed when that is not certain.

A candidate is accepted the strict way when its code is close to the
reference's, with the same imports and member offsets. Microsoft sometimes
rewrites a function, and then nothing is close. For TrayUI's virtual functions
there is a second way: the function keeps its vtable slot, so a candidate that
sits in the reference function's slot, measured from at least two functions
already matched the strict way, and still calls the same imports, is accepted
as that function."""
import struct
from dataclasses import dataclass, field

from . import HOOKED, anchors
from .code import Function, similarity

EXACT = 0.999   # getStuckPlace: an 18-byte leaf that must not change at all
CLOSE = 0.85    # observed 9549 -> 9807 minimum for a true match was 0.908

# TrayUI's own virtual functions: these share one vtable. GetAutoHideFlags is
# reached through a base class's vtable and PermitAutoHide is CTaskListWnd's.
TRAYUI_VTABLE = ('getStuckPlace', 'setAutoHideFlags', 'getDockedRect', 'unhide', 'slideWindow')
SLOT_ANCHORS = 2        # strict matches a slot must be measured from
VTABLE_SPAN = 0x1000    # two entries further apart are not one vtable


@dataclass
class Row:
    field: str
    anchor: str
    candidates: list
    rva: int | None = None
    size: int = 0
    similarity: float = 0.0
    imports_equal: bool = False
    offsets_equal: bool = False
    verdict: str = 'no candidate'


@dataclass
class MatchResult:
    rvas: dict | None
    rows: list = field(default_factory=list)
    reason: str | None = None


def _accept(fieldname, cand, ref):
    sim = similarity(cand, ref)
    imports = cand.import_calls() == ref.import_calls()
    offsets = cand.member_offsets() == ref.member_offsets()
    if fieldname == 'getAutoHideFlags':
        ok = offsets   # the anchor is the mod's own shape check; see the ledger
    else:
        ok = sim >= (EXACT if fieldname == 'getStuckPlace' else CLOSE) and imports and offsets
    return ok, sim, imports, offsets


def _pointers(pe, rva):
    """RVAs of every aligned 8-byte slot outside code that holds this
    function's address, as stored on disk (preferred base, before relocation)."""
    needle = struct.pack('<Q', pe.image_base + rva)
    out = []
    for _, va, _, ptr, raw_size, chars in pe.sections:
        if chars & 0x20000000:
            continue
        blob = pe.data[ptr:ptr + raw_size]
        i = blob.find(needle)
        while i >= 0:
            if i % 8 == 0:
                out.append(va + i)
            i = blob.find(needle, i + 1)
    return out


def _slot_distances(pe, rva, anchor_rva):
    return {s - a for s in _pointers(pe, rva) for a in _pointers(pe, anchor_rva)
            if abs(s - a) < VTABLE_SPAN}


def same_slot(new, ref, cand, ref_rva, pairs):
    """True if cand sits in the new build's vtable where ref_rva sits in the
    reference's, measured from each (reference, new) pair of functions already
    matched, with at least SLOT_ANCHORS of them to measure from and none
    disagreeing."""
    agree = 0
    for ref_anchor, new_anchor in pairs:
        want = _slot_distances(ref, ref_rva, ref_anchor)
        if not want:
            continue   # not in one vtable with this anchor in the reference
        if _slot_distances(new, cand, new_anchor) != want:
            return False
        agree += 1
    return agree >= SLOT_ANCHORS


def match_build(new, ref, ref_rvas):
    found, rows, turned_down = {}, [], {}
    order = ['getAutoHideFlags', 'getStuckPlace', 'setAutoHideFlags', 'unhide',
             'getDockedRect', 'slideWindow', 'permitAutoHide']
    sources = {
        'getAutoHideFlags': ('ScanForOffsets shape', lambda: anchors.get_auto_hide_flags(new)),
        'getStuckPlace': ('exact bytes', lambda: anchors.get_stuck_place(new)),
        'setAutoHideFlags': ('"IsAutoHideEnabled"', lambda: anchors.string_referrers(new, 'IsAutoHideEnabled')),
        'unhide': ('"AutoHideOnTaskbarShow"', lambda: anchors.string_referrers(new, 'AutoHideOnTaskbarShow')),
        'getDockedRect': ('calls getAutoHideFlags', lambda: anchors.callers(new, found['getAutoHideFlags'])),
        'slideWindow': ('duration loads', lambda: anchors.slide_window(new)),
        'permitAutoHide': ('member-offset pattern', lambda: anchors.permit_auto_hide(new)),
    }
    for name in order:
        anchor, find = sources[name]
        if name == 'getDockedRect' and 'getAutoHideFlags' not in found:
            rows.append(Row(name, anchor, [], verdict='needs getAutoHideFlags'))
            continue
        cands = find()
        row = Row(name, anchor, cands)
        ref_fn = Function(ref, ref_rvas[name])
        accepted = []
        starts = {start for start, _ in new.functions()}
        for c in cands:
            # Hooking a few bytes into a function passes every similarity
            # gate and crashes Explorer. Only getStuckPlace, a leaf with no
            # unwind data, is exempt: its anchor is an exact whole-function match.
            if name != 'getStuckPlace' and c not in starts:
                row.verdict = f'{c:#x} is not a function start'
                continue
            fn = Function(new, c)
            ok, sim, imp, off = _accept(name, fn, ref_fn)
            if ok:
                accepted.append((c, fn, sim, imp, off))
                continue
            turned_down.setdefault(name, []).append((c, fn, sim, imp, off))
            if sim > row.similarity:
                row.rva, row.size, row.similarity, row.imports_equal, row.offsets_equal = c, fn.size, sim, imp, off
                row.verdict = 'rejected'
        if len(accepted) == 1:
            c, fn, sim, imp, off = accepted[0]
            row.rva, row.size, row.similarity, row.imports_equal, row.offsets_equal = c, fn.size, sim, imp, off
            row.verdict = 'ok'
            found[name] = c
        elif len(accepted) > 1:
            row.verdict = f'ambiguous: {", ".join(hex(a[0]) for a in accepted)}'
        rows.append(row)

    # Second way, for a reworked TrayUI virtual: its vtable slot.
    pairs = [(ref_rvas[f], found[f]) for f in TRAYUI_VTABLE if f in found]
    for row in rows:
        if row.field in found or row.field not in TRAYUI_VTABLE:
            continue
        in_slot = [t for t in turned_down.get(row.field, [])
                   if t[3] and same_slot(new, ref, t[0], ref_rvas[row.field], pairs)]
        if len(in_slot) == 1:
            c, fn, sim, imp, off = in_slot[0]
            row.rva, row.size, row.similarity, row.imports_equal, row.offsets_equal = c, fn.size, sim, imp, off
            row.verdict = 'ok (vtable slot)'
            found[row.field] = c
    failed = [r for r in rows if r.field not in found]
    if failed:
        return MatchResult(None, rows, '; '.join(f'{r.field}: {r.verdict}' for r in failed))
    return MatchResult({f: found[f] for f in HOOKED}, rows)


def _version(pe):
    return tuple(int(p) for p in pe.file_version().split('.'))


def pick_reference(new, candidates):
    """The newest (DLL, PDB) candidate strictly older than the new build."""
    older = [c for c in candidates if _version(c[0]) < _version(new)]
    if not older:
        raise LookupError(f'no reference build older than {new.file_version()}')
    return max(older, key=lambda c: _version(c[0]))


def format_entry(new, rvas):
    guid, age = new.codeview()
    d4 = ', '.join(f'0x{b:02X}' for b in guid.bytes[8:])
    values = ', '.join(f'0x{rvas[f]:X}' for f in HOOKED)
    return (f'    {{L"{new.file_version()}",\n'
            f'     {{0x{guid.time_low:08X}, 0x{guid.time_mid:04X}, 0x{guid.time_hi_version:04X}, {{{d4}}}}},\n'
            f'     {age}, {values}}},\n')


def evidence_table(result):
    lines = ['| Function | Anchor | RVA | Size | Similarity | Imports | Offsets | Verdict |',
             '|---|---|---|---|---|---|---|---|']
    for r in result.rows:
        rva = f'0x{r.rva:X}' if r.rva is not None else '-'
        lines.append(f'| {r.field} | {r.anchor} | {rva} | {r.size} | {r.similarity:.3f} | '
                     f'{"same" if r.imports_equal else "differ"} | {"same" if r.offsets_equal else "differ"} | {r.verdict} |')
    return '\n'.join(lines)
