"""Reads and extends the mod's kKnownBuilds table and the README's list of
builds it covers."""
import re

from . import HOOKED

_ENTRY = re.compile(
    r'\{L"(?P<version>[\d.]+)",\s*'
    r'\{0x(?P<d1>[0-9A-Fa-f]{1,8}),\s*0x(?P<d2>[0-9A-Fa-f]{1,4}),\s*0x(?P<d3>[0-9A-Fa-f]{1,4}),\s*'
    r'\{(?P<d4>[^}]*)\}\},\s*(?P<age>\d+),\s*(?P<rvas>[0-9A-Fa-fx,\s]+)\}')
_TABLE = re.compile(r'(const KnownBuild kKnownBuilds\[\] = \{\n)(?P<body>.*?)(\n\};)', re.S)
_VERSION = re.compile(r'^(// @version\s+)(\d+)\.(\d+)\.(\d+)$', re.M)
_README_ROW = re.compile(r'^\| (10\.0\.\d+\.\d+) \| (10\.0\.\d+\.\d+) \|$', re.M)


def _table(text):
    m = _TABLE.search(text)
    if not m:
        raise ValueError('kKnownBuilds not found')
    return m


def known_builds(src_path):
    out = []
    for m in _ENTRY.finditer(_table(open(src_path).read()).group('body')):
        d4 = ''.join(f'{int(b, 16):02X}' for b in re.findall(r'0x([0-9A-Fa-f]+)', m['d4']))
        rvas = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]+)', m['rvas'])]
        entry = {'version': m['version'],
                 'pdb_key': f'{int(m["d1"], 16):08X}{int(m["d2"], 16):04X}{int(m["d3"], 16):04X}{d4}{int(m["age"]):X}'}
        entry.update(zip(HOOKED, rvas))
        out.append(entry)
    return out


def apply_entry(src_path, readme_path, entry_line, version, base_version):
    """Add entry_line to kKnownBuilds and the README, bump the mod's minor
    version, and return the new version."""
    text = open(src_path).read()
    new = _ENTRY.search(entry_line)
    if not new:
        raise ValueError('entry_line is not a KnownBuild initializer')
    new_guid = tuple(new.group('d1', 'd2', 'd3', 'd4', 'age'))
    for m in _ENTRY.finditer(_table(text).group('body')):
        if tuple(m.group('d1', 'd2', 'd3', 'd4', 'age')) == new_guid:
            raise ValueError(f'{version}: already in kKnownBuilds as {m["version"]}')

    t = _table(text)
    body = t.group('body').rstrip('\n') + '\n' + \
        f'    // Matched against {base_version} by tools/match-build.py.\n' + entry_line.rstrip('\n')
    text = text[:t.start('body')] + body + text[t.end('body'):]

    v = _VERSION.search(text)
    bumped = f'{v[2]}.{int(v[3]) + 1}.0'
    text = text[:v.start()] + f'{v[1]}{bumped}' + text[v.end():]
    with open(src_path, 'w') as f:
        f.write(text)

    readme = open(readme_path).read()
    rows = list(_README_ROW.finditer(readme))
    if not rows:
        raise ValueError('README build table not found')
    end = rows[-1].end()
    readme = readme[:end] + f'\n| {version} | {base_version} |' + readme[end:]
    with open(readme_path, 'w') as f:
        f.write(readme)
    return bumped
