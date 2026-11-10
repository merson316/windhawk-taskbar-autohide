import os
import re
import shutil
import subprocess
import sys

import pytest

from matcher.apply import apply_entry

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools'))
from check_build_pr import problems  # noqa: E402
from conftest import FAKE  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SRC, README = 'src/taskbar-autohide-any-edge.wh.cpp', 'README.md'
FAKE2 = FAKE.replace('9999', '9998').replace('0x11111111', '0x22222222')


def git(repo, *args, signed=True):
    pre = [] if signed else ['-c', 'commit.gpgsign=false']
    return subprocess.run(['git', '-C', repo, *pre, *args], check=True, capture_output=True, text=True).stdout.strip()


def key():
    return subprocess.run(['git', 'config', 'user.signingkey'], check=True, capture_output=True, text=True).stdout.strip()


def other_key(tmp_path):
    path = tmp_path / 'other'
    subprocess.run(['ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-f', str(path)], check=True)
    return str(path) + '.pub'


@pytest.fixture
def clone(tmp_path):
    repo = str(tmp_path / 'clone')
    subprocess.run(['git', 'clone', '-q', REPO, repo], check=True)
    return repo


def make_pr(repo, mutate, signed=True):
    base = git(repo, 'rev-parse', 'HEAD')
    mutate(repo)
    git(repo, 'commit', '-qam', 'build', signed=signed)
    return repo, base, git(repo, 'rev-parse', 'HEAD')


def add(entry, ref='10.0.26100.9807'):
    def f(repo):
        apply_entry(os.path.join(repo, SRC), os.path.join(repo, README), entry, re.search(r'L"([\d.]+)"', entry)[1], ref)
    return f


def edit(path, old, new):
    def f(repo):
        p = os.path.join(repo, path)
        s = open(p).read()
        assert old in s
        open(p, 'w').write(s.replace(old, new, 1))
    return f


def both(*fs):
    def f(repo):
        for g in fs:
            g(repo)
    return f


@pytest.fixture
def build_pr(clone):
    return make_pr(clone, add(FAKE))


@pytest.fixture
def build_pr_unsigned(clone):
    return make_pr(clone, add(FAKE), signed=False)


@pytest.fixture
def build_pr_two(clone):
    return make_pr(clone, both(add(FAKE), add(FAKE2)))


@pytest.fixture
def build_pr_edit_9807(clone):
    return make_pr(clone, both(add(FAKE), edit(SRC, '0x65250', '0x65251')))


@pytest.fixture
def build_pr_minor_plus_2(clone):
    def bump(repo):
        p = os.path.join(repo, SRC)
        s = open(p).read()
        m = re.search(r'(?m)^(// @version\s+)(\d+)\.(\d+)\.0$', s)
        open(p, 'w').write(s[:m.start()] + f'{m[1]}{m[2]}.{int(m[3]) + 1}.0' + s[m.end():])
    return make_pr(clone, both(add(FAKE), bump))


@pytest.fixture
def build_pr_readme_extra(clone):
    return make_pr(clone, both(add(FAKE), edit(README, '| 10.0.26100.9999 | 10.0.26100.9807 |',
                                               '| 10.0.26100.9999 | 10.0.26100.9807 |\n| 10.0.26100.9998 | 10.0.26100.9807 |')))


def test_clean_build_pr_passes(build_pr):
    assert problems(*build_pr, key()) == []


def test_unsigned_commit_refused(build_pr_unsigned):
    assert any('not signed by this PC' in p for p in problems(*build_pr_unsigned, key()))


def test_other_key_refused(build_pr, tmp_path):
    assert any('not signed by this PC' in p for p in problems(*build_pr, other_key(tmp_path)))


def test_private_key_of_another_identity_refused(build_pr, tmp_path):
    priv = tmp_path / 'ci-signing'
    subprocess.run(['ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-f', str(priv)], check=True)
    assert any('not signed by this PC' in p for p in problems(*build_pr, str(priv)))


def test_unreadable_signing_key_refused(build_pr, tmp_path):
    assert any('cannot read the signing key' in p for p in problems(*build_pr, str(tmp_path / 'missing')))


def test_passphrase_private_key_fails_fast(build_pr, tmp_path):
    priv = tmp_path / 'locked'
    subprocess.run(['ssh-keygen', '-q', '-t', 'ed25519', '-N', 'secret', '-f', str(priv)], check=True)
    assert any('cannot read the signing key' in p for p in problems(*build_pr, str(priv)))


def test_two_entries_refused(build_pr_two):
    assert 'adds 2 builds, expected 1' in problems(*build_pr_two, key())


def test_changed_entry_refused(build_pr_edit_9807):
    assert 'changes existing build 10.0.26100.9807' in problems(*build_pr_edit_9807, key())


def test_version_skip_refused(build_pr_minor_plus_2):
    assert any(p.startswith('version ') for p in problems(*build_pr_minor_plus_2, key()))


def test_readme_extra_line_refused(build_pr_readme_extra):
    assert 'README.md must add exactly one build row' in problems(*build_pr_readme_extra, key())


def test_clean_pr_has_no_beyond_message(build_pr):
    assert 'src/ or README.md has changes beyond the new build entry' not in problems(*build_pr, key())


def test_removed_entry_refused(clone):
    def rm(repo):
        p = os.path.join(repo, SRC)
        t = open(p).read()
        i = t.index('    {L"10.0.26100.9807"')
        j = t.index('0x35C40},\n', i) + len('0x35C40},\n')
        open(p, 'w').write(t[:i] + t[j:])
    r = make_pr(clone, both(add(FAKE), rm))
    assert 'removes existing build 10.0.26100.9807' in problems(*r, key())


def test_readme_row_removed_refused(clone):
    r = make_pr(clone, both(add(FAKE), edit(README, '| 10.0.26100.9807 | 10.0.26100.9549 |\n', '')))
    assert 'README.md must add exactly one build row' in problems(*r, key())


def test_readme_removed_triple_dash_line_refused(clone):
    seed = make_pr(clone, edit(README, '| 10.0.26100.9807 | 10.0.26100.9549 |\n', '| 10.0.26100.9807 | 10.0.26100.9549 |\n---\n'))
    # The PR adds its row and deletes the '---' line: one row added, one line removed.
    r = make_pr(clone, both(add(FAKE), edit(README, '---\n', '')))
    assert r[1] == seed[2]
    assert 'README.md must add exactly one build row' in problems(*r, key())


def test_extra_src_edit_refused(clone):
    r = make_pr(clone, both(add(FAKE), edit(SRC, '#include <windhawk_utils.h>', '#include <windhawk_utils.h>\n// sneaky')))
    assert 'src/ or README.md has changes beyond the new build entry' in problems(*r, key())


def test_other_file_refused(clone):
    def f(repo):
        open(os.path.join(repo, 'evil.txt'), 'w').write('x')
        git(repo, 'add', 'evil.txt')
    r = make_pr(clone, both(add(FAKE), f))
    assert 'changes files other than src/ and README.md: evil.txt' in problems(*r, key())


def test_duplicate_version_refused(clone):
    dup = FAKE.replace('9999', '9807').replace('0x11111111', '0x33333333')
    r = make_pr(clone, add(dup))
    assert 'adds a duplicate of build 10.0.26100.9807' in problems(*r, key())


BEYOND = 'src/ or README.md has changes beyond the new build entry'


def test_trailing_code_on_entry_line_refused(clone):
    evil = FAKE.rstrip('\n') + ' static int evil = system("calc");\n'
    r = make_pr(clone, add(evil))
    assert BEYOND in problems(*r, key())


def test_expression_in_guid_refused(clone):
    # apply_entry itself refuses such an entry, so plant it in the file after.
    r = make_pr(clone, both(add(FAKE), edit(SRC, '{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11}',
                                            '{(system("calc"),0x11), 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11}')))
    found = problems(*r, key())
    assert 'adds 0 builds, expected 1' in found
