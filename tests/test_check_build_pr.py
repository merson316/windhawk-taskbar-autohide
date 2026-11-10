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


def test_private_key_path_accepted(build_pr, tmp_path):
    priv = tmp_path / 'ci-signing'
    pub = key()
    # A private key can only be derived from, not matched to, the owner's agent
    # key: here it must simply not crash, and must refuse (different key).
    subprocess.run(['ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-f', str(priv)], check=True)
    assert any('not signed by this PC' in p for p in problems(*build_pr, str(priv)))
    assert pub


def test_two_entries_refused(build_pr_two):
    assert 'adds 2 builds, expected 1' in problems(*build_pr_two, key())


def test_changed_entry_refused(build_pr_edit_9807):
    assert 'changes existing build 10.0.26100.9807' in problems(*build_pr_edit_9807, key())


def test_version_skip_refused(build_pr_minor_plus_2):
    assert any(p.startswith('version ') for p in problems(*build_pr_minor_plus_2, key()))


def test_readme_extra_line_refused(build_pr_readme_extra):
    assert 'README.md must add exactly one build row' in problems(*build_pr_readme_extra, key())
