"""Decides, for each hooked function, which anchor candidate in the new build
is the reference build's function, and fails closed when that is not certain."""
from dataclasses import dataclass, field

from . import HOOKED, anchors
from .code import Function, similarity

EXACT = 0.999   # getStuckPlace: an 18-byte leaf that must not change at all
CLOSE = 0.85    # observed 9549 -> 9807 minimum for a true match was 0.908


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


def match_build(new, ref, ref_rvas):
    found, rows = {}, []
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
        for c in cands:
            fn = Function(new, c)
            ok, sim, imp, off = _accept(name, fn, ref_fn)
            if ok:
                accepted.append((c, fn, sim, imp, off))
            elif sim > row.similarity:
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
    failed = [r for r in rows if r.verdict != 'ok']
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
