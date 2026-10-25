"""release.sh --pr runs with the signing key loaded, so it must refuse a PR
that could change what runs: from a fork, touching anything but src/ and
README.md in any commit, containing a merge or a non-regular file."""
import json
import os
import re
import subprocess

import pytest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))


def git(cwd, *args):
    return subprocess.run(['git', '-C', str(cwd), *args], check=True, capture_output=True, text=True).stdout.strip()


@pytest.fixture
def setup(tmp_path):
    origin = tmp_path / 'origin.git'
    subprocess.run(['git', 'clone', '-q', '--bare', REPO, str(origin)], check=True)
    author = tmp_path / 'author'
    subprocess.run(['git', 'clone', '-q', str(origin), str(author)], check=True)
    return tmp_path, origin, author


def open_pr(author, build):
    """Commit on a branch in the author clone and publish it as PR #7."""
    git(author, 'switch', '-q', '-c', 'build/test', 'origin/main')
    build(author)
    sha = git(author, 'rev-parse', 'HEAD')
    git(author, 'push', '-q', 'origin', 'HEAD:refs/pull/7/head')
    return sha


def run_release(tmp_path, origin, pr, extra_env=None):
    rel = tmp_path / 'releaser'
    subprocess.run(['git', 'clone', '-q', str(origin), str(rel)], check=True)
    subprocess.run(['cp', os.path.join(REPO, 'tools/release.sh'), rel / 'tools/release.sh'], check=True)
    git(rel, 'update-index', '--assume-unchanged', 'tools/release.sh')
    bin_dir = tmp_path / 'bin'
    bin_dir.mkdir(exist_ok=True)
    (bin_dir / 'gh').write_text(f"#!/bin/sh\ncat <<'JSON'\n{json.dumps(pr)}\nJSON\n")
    (bin_dir / 'gh').chmod(0o755)
    env = {**os.environ, 'PATH': f'{bin_dir}:{os.environ["PATH"]}', **(extra_env or {})}
    p = subprocess.run(['tools/release.sh', '9.9.9', '--pr', '7'], cwd=rel, env=env,
                       capture_output=True, text=True, input='no\n')
    p.main_moved = git(rel, 'rev-parse', 'main') != git(rel, 'rev-parse', 'origin/main')
    return p


def pr_json(sha, cross=False, draft=False,
            body='- [x] Compiled\n- [x] Works\nTested on OS build (winver): `26300.9999`'):
    return {'isCrossRepository': cross, 'isDraft': draft, 'headRefOid': sha, 'headRefName': 'build/test',
            'baseRefName': 'main', 'body': body}


def commit_file(path, text):
    def build(repo):
        (repo / path).parent.mkdir(parents=True, exist_ok=True)
        (repo / path).write_text(text)
        git(repo, 'add', path)
        git(repo, 'commit', '-q', '-m', f'test: {path}')
    return build


def test_refuses_fork(setup):
    tmp, origin, author = setup
    sha = open_pr(author, commit_file('src/x.txt', 'x'))
    p = run_release(tmp, origin, pr_json(sha, cross=True))
    assert p.returncode != 0 and 'from a fork' in p.stderr


def test_refuses_path_outside_allowlist_in_any_commit(setup):
    tmp, origin, author = setup

    def build(repo):
        commit_file('tools/evil.sh', 'x')(repo)
        git(repo, 'rm', '-q', 'tools/evil.sh')
        git(repo, 'commit', '-q', '-m', 'test: hide it again')   # net diff is clean
        commit_file('src/x.txt', 'x')(repo)
    p = run_release(tmp, origin, pr_json(open_pr(author, build)))
    assert p.returncode != 0 and 'tools/evil.sh' in p.stderr


def test_refuses_merge_commit(setup):
    tmp, origin, author = setup

    def build(repo):
        git(repo, 'switch', '-q', '-c', 'side', 'origin/main')
        commit_file('src/y.txt', 'y')(repo)
        git(repo, 'switch', '-q', 'build/test')
        commit_file('src/x.txt', 'x')(repo)
        git(repo, 'merge', '-q', '--no-ff', '-m', 'test: merge', 'side')
    p = run_release(tmp, origin, pr_json(open_pr(author, build)))
    assert p.returncode != 0 and 'merge' in p.stderr


def test_refuses_symlink_under_src(setup):
    tmp, origin, author = setup

    def build(repo):
        os.symlink('../tools/release.sh', repo / 'src/link')
        git(repo, 'add', 'src/link')
        git(repo, 'commit', '-q', '-m', 'test: symlink')
    p = run_release(tmp, origin, pr_json(open_pr(author, build)))
    assert p.returncode != 0 and 'src/link' in p.stderr


def bump_version(repo, version='9.9.9'):
    src = repo / 'src/taskbar-autohide-any-edge.wh.cpp'
    src.write_text(re.sub(r'(?m)^(// @version\s+)\S+$', rf'\g<1>{version}', src.read_text()))
    git(repo, 'commit', '-q', '-am', f'test: version {version}')


def test_clean_pr_reaches_confirmation(setup):
    tmp, origin, author = setup

    def build(repo):
        bump_version(repo)
        commit_file('src/x.txt', 'x')(repo)
        git(repo, 'rm', '-q', 'src/x.txt')
        git(repo, 'commit', '-q', '-m', 'test: delete under src')   # deletions are allowed
        commit_file('src/y.txt', 'y')(repo)
    p = run_release(tmp, origin, pr_json(open_pr(author, build)))
    # stdin is not a terminal, so read -p prints no prompt; the commits are shown
    # and the empty answer is refused.
    assert 'test: src/y.txt' in p.stdout
    assert p.returncode == 1 and 'not confirmed' in p.stderr


def test_tag_named_main_cannot_hide_commits(setup):
    tmp, origin, author = setup

    def build(repo):
        commit_file('tools/evil.sh', 'x')(repo)
        git(repo, 'tag', '-a', '-m', 'decoy', 'main')   # makes "main" resolve to the evil commit
        git(repo, 'push', '-q', 'origin', 'refs/tags/main')
        commit_file('src/x.txt', 'x')(repo)
    p = run_release(tmp, origin, pr_json(open_pr(author, build)))
    assert p.returncode != 0
    assert 'not confirmed' not in p.stderr, 'reached the confirmation with the hidden commit unchecked'
    assert 'tools/evil.sh' in p.stderr or 'refusing' in p.stderr


def test_refuses_without_signing_before_touching_anything(setup):
    tmp, origin, author = setup

    def build(repo):
        bump_version(repo)
    no_signer = {'GIT_CONFIG_COUNT': '1', 'GIT_CONFIG_KEY_0': 'gpg.ssh.program', 'GIT_CONFIG_VALUE_0': '/bin/false'}
    p = run_release(tmp, origin, pr_json(open_pr(author, build)), no_signer)
    assert p.returncode != 0 and 'signing is unavailable' in p.stderr
    assert not p.main_moved


def test_refuses_wrong_version_before_rebasing(setup):
    tmp, origin, author = setup
    p = run_release(tmp, origin, pr_json(open_pr(author, commit_file('src/x.txt', 'x'))))
    assert p.returncode != 0 and 'not at version 9.9.9' in p.stderr
    assert 'not confirmed' not in p.stderr
    assert not p.main_moved


def test_refuses_draft_pr(setup):
    tmp, origin, author = setup
    p = run_release(tmp, origin, pr_json(open_pr(author, bump_version), draft=True))
    assert p.returncode != 0 and 'still a draft' in p.stderr
    assert not p.main_moved


def test_refuses_unticked_checklist(setup):
    tmp, origin, author = setup
    body = '- [x] Compiled\n- [ ] Works\nTested on OS build (winver): `26300.9999`'
    p = run_release(tmp, origin, pr_json(open_pr(author, bump_version), body=body))
    assert p.returncode != 0 and 'unticked' in p.stderr
    assert not p.main_moved
