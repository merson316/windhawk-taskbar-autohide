"""release.sh --pr runs with the signing key loaded, so it must refuse a PR
that could change what runs: from a fork, touching anything but src/ and
README.md in any commit, containing a merge or a non-regular file."""
import http.server
import json
import os
import re
import sys
import subprocess
import threading

import pytest

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools'))
from conftest import FAKE  # noqa: E402
from matcher.apply import apply_entry  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))


def git(cwd, *args):
    return subprocess.run(['git', '-C', str(cwd), *args], check=True, capture_output=True, text=True).stdout.strip()


@pytest.fixture
def setup(tmp_path):
    origin = tmp_path / 'origin.git'
    subprocess.run(['git', 'clone', '-q', '--bare', REPO, str(origin)], check=True)
    # Clones check out main even when this repo is on a feature branch.
    git(origin, 'symbolic-ref', 'HEAD', 'refs/heads/main')
    # ...and test what is checked out here, not whatever main happens to be.
    git(origin, 'update-ref', 'refs/heads/main', git(REPO, 'rev-parse', 'HEAD'))
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


# pr.json's "state" MERGED makes `pr close` refuse, as GitHub does once main
# holds the PR's head.
FAKE_GH = """#!/bin/sh
echo "$@" >> "$GH_DIR/gh.log"
case "$1 $2" in
  "pr view") cat "$GH_DIR/pr.json" ;;
  "pr close")
    grep -q '"state": "MERGED"' "$GH_DIR/pr.json" &&
      { echo "can't be closed because it was already merged" >&2; exit 1; }
    exit 0 ;;
  "release create")
    tag=$3
    while [ $# -gt 0 ]; do [ "$1" = --notes-file ] && cp "$2" "$GH_DIR/notes.md"; shift; done
    [ -e "$GH_DIR/fail-release" ] && { echo "boom" >&2; exit 1; }
    touch "$GH_DIR/released-$tag"
    echo "https://github.com/example/repo/releases/tag/$tag" ;;
  "release view")
    [ -e "$GH_DIR/released-$3" ] || exit 1
    echo "{\\"url\\": \\"https://github.com/example/repo/releases/tag/$3\\"}" ;;
  *) exit 0 ;;
esac
"""


@pytest.fixture
def msdl():
    """A symbol server that has nothing: keeps release_notes off the network."""
    class H(http.server.BaseHTTPRequestHandler):
        def _reply(self):
            self.send_response(404)
            self.end_headers()
        do_HEAD = do_GET = _reply

        def log_message(self, *a):
            pass

    srv = http.server.HTTPServer(('127.0.0.1', 0), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    yield f'http://127.0.0.1:{srv.server_port}'
    srv.shutdown()


def run_release(tmp_path, origin, pr, extra_env=None, args=('--pr', '7'), version='9.9.9', stdin='no\n'):
    rel = tmp_path / 'releaser'
    if not rel.exists():
        subprocess.run(['git', 'clone', '-q', str(origin), str(rel)], check=True)
        subprocess.run(['cp', os.path.join(REPO, 'tools/release.sh'), rel / 'tools/release.sh'], check=True)
        os.symlink(os.path.join(REPO, '.venv'), rel / '.venv')
        (rel / '.git/info/exclude').write_text('.venv\n')
        # No network: the real check-builds.py asks Winbindex.
        (rel / 'tools/check-builds.py').write_text('')
        git(rel, 'update-index', '--assume-unchanged', 'tools/release.sh', 'tools/check-builds.py')
    gh_dir = tmp_path / 'gh'
    gh_dir.mkdir(exist_ok=True)
    (gh_dir / 'pr.json').write_text(json.dumps(pr))
    bin_dir = tmp_path / 'bin'
    bin_dir.mkdir(exist_ok=True)
    (bin_dir / 'gh').write_text(FAKE_GH)
    (bin_dir / 'gh').chmod(0o755)
    env = {**os.environ, 'PATH': f'{bin_dir}:{os.environ["PATH"]}', 'GH_DIR': str(gh_dir),
           'MSDL_BASE': 'http://127.0.0.1:9', **(extra_env or {})}
    p = subprocess.run(['tools/release.sh', version, *args], cwd=rel, env=env,
                       capture_output=True, text=True, input=stdin)
    p.main_moved = git(rel, 'rev-parse', 'main') != git(rel, 'rev-parse', 'origin/main')
    p.gh_log = (gh_dir / 'gh.log').read_text() if (gh_dir / 'gh.log').exists() else ''
    p.notes = (gh_dir / 'notes.md').read_text() if (gh_dir / 'notes.md').exists() else ''
    return p


def pr_json(sha, cross=False, draft=False,
            body='- [x] Compiled\n- [x] Works\nTested on OS build (winver): `26300.9999`', branch='build/test',
            state='OPEN'):
    return {'isCrossRepository': cross, 'isDraft': draft, 'headRefOid': sha, 'headRefName': branch,
            'baseRefName': 'main', 'body': body, 'state': state}


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


# --- release.sh --auto -------------------------------------------------------

SRC = 'src/taskbar-autohide-any-edge.wh.cpp'


def next_minor(author):
    v = re.search(r'(?m)^// @version\s+(\d+)\.(\d+)\.\d+$', (author / SRC).read_text())
    return f'{v[1]}.{int(v[2]) + 1}.0'


def watcher_pr(repo, signed=True, message=None):
    """A PR shaped like the watcher's: one entry applied, one commit."""
    apply_entry(str(repo / SRC), str(repo / 'README.md'), FAKE, '10.0.26100.9999', '10.0.26100.9807')
    git(repo, 'add', SRC, 'README.md')
    msg = message or ('feat: built-in addresses for taskbar.dll 10.0.26100.9999\n\n'
                      'Matched against 10.0.26100.9807 by tools/match-build.py.\n')
    pre = [] if signed else ['-c', 'commit.gpgsign=false']
    git(repo, *pre, 'commit', '-q', '-m', msg)


def auto(tmp, origin, author, build=watcher_pr, sha=None, **kw):
    version = next_minor(author)
    head = open_pr(author, build)
    pr = pr_json(head, **kw)
    return version, head, pr, ['--pr', '7', '--auto', '--sha', sha or head]


def test_auto_skips_draft(setup, msdl):
    # A draft is the owner's stop signal, not a refusal: its own exit code.
    tmp, origin, author = setup
    v, head, pr, args = auto(tmp, origin, author, draft=True)
    p = run_release(tmp, origin, pr, {'MSDL_BASE': msdl}, args, v)
    assert p.returncode == 4 and 'still a draft' in p.stderr
    assert not p.main_moved


def test_auto_refuses_branch_outside_build(setup, msdl):
    tmp, origin, author = setup
    v, head, pr, args = auto(tmp, origin, author, branch='feature/build/x')
    p = run_release(tmp, origin, pr, {'MSDL_BASE': msdl}, args, v)
    assert p.returncode == 3 and 'PR #7 is not from a build/ branch; refusing' in p.stderr
    assert not p.main_moved


def test_auto_ignores_checklist(setup, msdl):
    tmp, origin, author = setup
    v, head, pr, args = auto(tmp, origin, author, body='- [ ] Compiled\n- [ ] Works')
    p = run_release(tmp, origin, pr, {'MSDL_BASE': msdl}, args, v)
    assert p.returncode == 0, p.stderr


def test_auto_refuses_moved_sha(setup, msdl):
    tmp, origin, author = setup
    v, head, pr, args = auto(tmp, origin, author, sha='0' * 40)
    p = run_release(tmp, origin, pr, {'MSDL_BASE': msdl}, args, v)
    assert p.returncode == 3 and 'moved' in p.stderr
    assert not p.main_moved


def test_auto_refuses_untrusted(setup, msdl):
    tmp, origin, author = setup
    v, head, pr, args = auto(tmp, origin, author, build=lambda r: watcher_pr(r, signed=False))
    p = run_release(tmp, origin, pr, {'MSDL_BASE': msdl}, args, v)
    assert p.returncode == 3 and 'not signed by this PC' in p.stderr
    assert not p.main_moved


def test_auto_releases(setup, msdl):
    tmp, origin, author = setup
    v, head, pr, args = auto(tmp, origin, author)
    p = run_release(tmp, origin, pr, {'MSDL_BASE': msdl}, args, v, stdin='')
    assert p.returncode == 0, p.stderr
    assert git(origin, 'log', '--format=%G?', 'main~1..main').split() == ['G']
    assert git(origin, 'cat-file', '-t', f'v{v}') == 'tag'
    subprocess.run(['git', '-C', str(tmp / 'releaser'), 'tag', '-v', f'v{v}'], check=True, capture_output=True)
    assert f'release create v{v}' in p.gh_log
    assert f'pr comment 7 --body Released automatically in v{v} once CI passed.' in p.gh_log
    assert 'pr close 7\n' in p.gh_log
    assert ('Built-in addresses for taskbar.dll 10.0.26100.9999, matched automatically against '
            '10.0.26100.9807. Not tested by hand.') in p.notes
    assert not (tmp / 'releaser/docs/compatibility.json').read_text().count(v)


def test_auto_notes_fall_back_to_readme_row(setup, msdl):
    tmp, origin, author = setup
    v, head, pr, args = auto(tmp, origin, author, build=lambda r: watcher_pr(r, message='feat: add a build\n'))
    p = run_release(tmp, origin, pr, {'MSDL_BASE': msdl}, args, v)
    assert p.returncode == 0, p.stderr
    assert 'taskbar.dll 10.0.26100.9999, matched automatically against 10.0.26100.9807.' in p.notes


def test_auto_refuses_when_build_unknown(setup, msdl):
    tmp, origin, author = setup

    def build(repo):   # no watcher message, and no README row to fall back on
        apply_entry(str(repo / SRC), str(repo / 'README.md'), FAKE, '10.0.26100.9999', '10.0.26100.9807')
        git(repo, 'checkout', '-q', '--', 'README.md')
        git(repo, 'commit', '-q', '-am', 'feat: add a build')
    v, head, pr, args = auto(tmp, origin, author, build=build)
    p = run_release(tmp, origin, pr, {'MSDL_BASE': msdl}, args, v)
    assert p.returncode == 3 and 'refused' in p.stderr
    assert not p.main_moved


def test_auto_resumes_after_tag(setup, msdl):
    tmp, origin, author = setup
    v, head, pr, args = auto(tmp, origin, author)
    (tmp / 'gh').mkdir()
    (tmp / 'gh/fail-release').write_text('')
    p = run_release(tmp, origin, pr, {'MSDL_BASE': msdl}, args, v)
    assert p.returncode not in (0, 3), 'a failed gh call is not a refusal'
    assert git(origin, 'tag', '-l', f'v{v}') == f'v{v}'
    (tmp / 'gh/fail-release').unlink()
    p = run_release(tmp, origin, pr, {'MSDL_BASE': msdl}, args, v)
    assert p.returncode == 0, p.stderr
    assert 'resuming' in p.stdout
    assert git(origin, 'tag', '-l', 'v*').split().count(f'v{v}') == 1
    assert p.gh_log.count(f'release create v{v}') == 2
    assert 'Not tested by hand.' in p.notes


def test_auto_without_signing_is_a_failure_not_a_refusal(setup, msdl):
    tmp, origin, author = setup
    v, head, pr, args = auto(tmp, origin, author)
    no_signer = {'MSDL_BASE': msdl, 'GIT_CONFIG_COUNT': '1', 'GIT_CONFIG_KEY_0': 'gpg.ssh.program',
                 'GIT_CONFIG_VALUE_0': '/bin/false'}
    p = run_release(tmp, origin, pr, no_signer, args, v)
    assert p.returncode == 1 and 'signing is unavailable' in p.stderr


def test_auto_needs_sha(setup, msdl):
    tmp, origin, author = setup
    p = run_release(tmp, origin, pr_json('x'), {'MSDL_BASE': msdl}, ['--pr', '7', '--auto'], '9.9.9')
    assert p.returncode == 2


def test_auto_finishes_when_github_marked_the_pr_merged(setup, msdl):
    # Pushing main makes GitHub mark the PR merged (its head is on main), so
    # `pr close` fails. The release is out by then: that is not a failure.
    tmp, origin, author = setup
    v, head, pr, args = auto(tmp, origin, author, state='MERGED')
    git(author, 'push', '-q', 'origin', 'HEAD:refs/heads/build/test')
    p = run_release(tmp, origin, pr, {'MSDL_BASE': msdl}, args, v)
    assert p.returncode == 0, p.stderr
    assert f'release create v{v}' in p.gh_log
    assert f'pr comment 7 --body Released automatically in v{v}' in p.gh_log
    assert 'pr close' not in p.gh_log
    assert 'build/test' not in git(origin, 'branch', '--list'), 'branch not deleted'


def test_auto_branch_delete_failure_is_a_warning(setup, msdl):
    tmp, origin, author = setup
    v, head, pr, args = auto(tmp, origin, author)
    git(author, 'push', '-q', 'origin', 'HEAD:refs/heads/build/test')
    hook = origin / 'hooks/pre-receive'
    hook.write_text('#!/bin/sh\nwhile read old new ref; do [ "$ref" = refs/heads/build/test ] && exit 1; done\nexit 0\n')
    hook.chmod(0o755)
    p = run_release(tmp, origin, pr, {'MSDL_BASE': msdl}, args, v)
    assert p.returncode == 0, p.stderr
    assert 'warning: could not delete branch build/test' in p.stderr


def test_auto_rerun_after_release_is_already_released(setup, msdl):
    # The release exists: a re-run says so, prints its URL and exits 0, so
    # the watcher still installs it and closes the stale PRs.
    tmp, origin, author = setup
    v, head, pr, args = auto(tmp, origin, author)
    p = run_release(tmp, origin, pr, {'MSDL_BASE': msdl}, args, v)
    assert p.returncode == 0, p.stderr
    p = run_release(tmp, origin, {**pr, 'state': 'MERGED'}, {'MSDL_BASE': msdl}, args, v)
    assert p.returncode == 0, p.stderr
    assert f'v{v} is already released' in p.stdout
    assert f'https://github.com/example/repo/releases/tag/v{v}' in p.stdout.splitlines()
    assert p.gh_log.count('release create') == 1
    assert p.gh_log.count('pr comment') == 1, 'commented twice'


def test_auto_does_not_resume_another_prs_release(setup, msdl):
    # PR A pushed main and v1.x.0, then failed. PR B bumps to the same
    # version from the old main: it must not publish A's tag with B's notes.
    tmp, origin, author = setup
    v, head, pr, args = auto(tmp, origin, author)
    (tmp / 'gh').mkdir()
    (tmp / 'gh/fail-release').write_text('')
    p = run_release(tmp, origin, pr, {'MSDL_BASE': msdl}, args, v)
    assert p.returncode not in (0, 3)
    assert git(origin, 'tag', '-l', f'v{v}') == f'v{v}'
    (tmp / 'gh/fail-release').unlink()
    git(author, 'switch', '-q', '-c', 'build/other', 'origin/main')
    other_entry = FAKE.replace('9999', '9998').replace('0x11111111', '0x22222222')
    apply_entry(str(author / SRC), str(author / 'README.md'), other_entry, '10.0.26100.9998', '10.0.26100.9807')
    git(author, 'commit', '-q', '-am', 'feat: built-in addresses for taskbar.dll 10.0.26100.9998\n\n'
        'Matched against 10.0.26100.9807 by tools/match-build.py.\n')
    other = git(author, 'rev-parse', 'HEAD')
    git(author, 'push', '-q', '-f', 'origin', 'HEAD:refs/pull/7/head')
    p = run_release(tmp, origin, pr_json(other, branch='build/other'),
                    {'MSDL_BASE': msdl}, ['--pr', '7', '--auto', '--sha', other], v)
    assert p.returncode == 1, p.stdout + p.stderr
    assert "another PR's release" in p.stderr
    assert p.gh_log.count('release create') == 1, 'published'
