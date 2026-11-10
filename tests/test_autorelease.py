"""The watcher releases its own build PRs once CI passes on their exact head,
oldest first, in a throwaway clone; the owner's checkout is never touched."""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

import pytest

sys.path.insert(0, os.path.dirname(__file__))
from conftest import FAKE  # noqa: E402
from test_watch import msdl, stage  # noqa: E402,F401  (msdl is a fixture)

from matcher import reference  # noqa: E402
from matcher.apply import apply_entry  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SRC = 'src/taskbar-autohide-any-edge.wh.cpp'
OLD = '2026-01-01T00:00:00Z'


def git(cwd, *args):
    return subprocess.run(['git', '-C', str(cwd), *args], check=True, capture_output=True, text=True).stdout.strip()


class World:
    def __init__(self, tmp):
        self.tmp = tmp
        self.origin = tmp / 'origin.git'
        self.author = tmp / 'author'
        self.checkout = tmp / 'repo'
        self.state = tmp / 'gh.json'
        self.gh_log = tmp / 'gh.log'
        self.notes = tmp / 'notify.log'
        self.bin = tmp / 'bin'

    def gh(self, **changes):
        state = json.loads(self.state.read_text()) if self.state.exists() else {'prs': []}
        state.update(changes)
        self.state.write_text(json.dumps(state))
        return state

    def pr(self, number, build='10.0.26100.9999', created=OLD, draft=False, cross=False, signed=True):
        """A PR shaped like the watcher's for `build`, published as #number."""
        branch = f'build/{build}'
        git(self.author, 'fetch', '-q', 'origin')
        git(self.author, 'switch', '-q', '-C', branch, 'origin/main')
        entry = FAKE.replace('10.0.26100.9999', build)
        apply_entry(str(self.author / SRC), str(self.author / 'README.md'), entry, build, '10.0.26100.9807')
        git(self.author, 'add', SRC, 'README.md')
        pre = [] if signed else ['-c', 'commit.gpgsign=false']
        git(self.author, *pre, 'commit', '-q', '-m', f'feat: built-in addresses for taskbar.dll {build}\n\n'
            'Matched against 10.0.26100.9807 by tools/match-build.py.\n')
        sha = git(self.author, 'rev-parse', 'HEAD')
        git(self.author, 'push', '-q', '-f', 'origin', f'HEAD:refs/heads/{branch}', f'HEAD:refs/pull/{number}/head')
        state = self.gh()
        state['prs'].append({'number': number, 'headRefName': branch, 'headRefOid': sha, 'createdAt': created,
                             'isDraft': draft, 'isCrossRepository': cross, 'baseRefName': 'main', 'body': ''})
        self.gh(prs=state['prs'])
        return sha

    def next_version(self):
        v = re.search(r'(?m)^// @version\s+(\d+)\.(\d+)\.\d+$', git(self.origin, 'show', f'main:{SRC}'))
        return f'{v[1]}.{int(v[2]) + 1}.0'

    def log(self):
        return self.gh_log.read_text() if self.gh_log.exists() else ''

    def notified(self):
        return self.notes.read_text() if self.notes.exists() else ''

    def env(self, msdl_base):
        return {'PATH': f'{self.bin}:{os.environ["PATH"]}', 'GH_STATE': str(self.state), 'GH_LOG': str(self.gh_log),
                'GH_ORIGIN': str(self.origin), 'MSDL_BASE': msdl_base, 'CI_EVERY': '0.05', 'CI_WAIT': '2',
                'NOTIFY_CMD': f'sh -c \'echo "$*" >> {self.notes}\' notify',
                # Installing stops at the layout check: no Windhawk here.
                'WINDHAWK_DIR': str(self.tmp / 'no-windhawk'), 'WINDHAWK_DATA': str(self.tmp / 'no-windhawk-data'),
                'INSTALL_STAGING': str(self.tmp / 'staging'), 'REG_CMD': 'false', 'SCHTASKS_CMD': 'false'}


@pytest.fixture
def world(tmp_path):
    """origin/main holds the tools being edited here (a clone made to release
    must run them) but not 9807, and a check-builds.py that stays off the
    network. The owner's checkout is a clone of it."""
    w = World(tmp_path)
    subprocess.run(['git', 'clone', '-q', '--bare', REPO, str(w.origin)], check=True)
    git(w.origin, 'symbolic-ref', 'HEAD', 'refs/heads/main')
    git(w.origin, 'update-ref', 'refs/heads/main', git(REPO, 'rev-parse', 'HEAD'))
    subprocess.run(['git', 'clone', '-q', str(w.origin), str(w.author)], check=True)
    src = w.author / SRC
    src.write_text(re.sub(r'    // Matched against 10\.0\.26100\.9549.*?0x35C40\},\n', '', src.read_text(), flags=re.S))
    for d in ('tools', '.github'):
        shutil.copytree(os.path.join(REPO, d), w.author / d, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('__pycache__'))
    (w.author / 'tools/check-builds.py').write_text('')
    git(w.author, 'add', '-A', 'src', 'tools', '.github')
    git(w.author, 'commit', '-q', '-m', 'test: drop 9807, tools under test')
    git(w.author, 'push', '-q', 'origin', 'HEAD:main')
    subprocess.run(['git', 'clone', '-q', str(w.origin), str(w.checkout)], check=True)
    os.symlink(os.path.join(REPO, '.venv'), w.checkout / '.venv')
    (w.checkout / '.git/info/exclude').write_text('.venv\n')
    w.bin.mkdir()
    shutil.copy(os.path.join(REPO, 'tests/fake_gh.py'), w.bin / 'gh')
    (w.bin / 'gh').chmod(0o755)
    w.gh()
    return w


@pytest.fixture
def ar(world, msdl, monkeypatch):
    """autorelease, run in-process from the owner's checkout."""
    for k, v in world.env(msdl).items():
        monkeypatch.setenv(k, v)
    monkeypatch.chdir(world.checkout)
    import autorelease
    import watch_build
    monkeypatch.setattr(watch_build, 'REPO', str(world.checkout))
    (world.tmp / 't').mkdir()
    monkeypatch.setattr(tempfile, 'tempdir', str(world.tmp / 't'))
    monkeypatch.setattr(reference, 'CACHE', str(world.tmp / 'cache'))
    return autorelease


def test_green_pr_released(world, ar):
    world.pr(5)
    v = world.next_version()
    assert ar.release_pending(set()) == [v]
    assert git(world.origin, 'cat-file', '-t', f'v{v}') == 'tag'
    assert f'release create v{v}' in world.log()
    assert f'v{v} released; install it in Windhawk by hand' in world.notified()
    assert 'windhawk.ini' in world.notified()
    assert not os.listdir(world.tmp / 't'), 'clone left behind'


def test_pending_pr_left(world, ar):
    sha = world.pr(5)
    world.gh(checks={sha: 'in_progress'})
    assert ar.release_pending(set()) == []
    assert 'release create' not in world.log()
    assert world.notified() == ''


def test_opened_pr_waits_for_ci(world, ar, monkeypatch):
    sha = world.pr(5)
    world.gh(checks={sha: 'in_progress'})
    monkeypatch.setenv('CI_WAIT', '0.3')
    assert ar.release_pending({5}) == []
    assert world.log().count('check-runs') > 2, 'did not poll'


def test_failed_ci_toasts_once(world, ar):
    sha = world.pr(5)
    world.gh(checks={sha: 'failure'})
    assert ar.release_pending(set()) == []
    assert ar.release_pending(set()) == []
    assert world.notified().count('Taskbar 10.0.26100.9999: CI failed') == 1
    assert 'release create' not in world.log()


def test_ci_state_needs_both_required_checks(world, ar):
    sha = world.pr(5)
    one = [{'name': 'Python tests', 'status': 'completed', 'conclusion': 'success'}]
    world.gh(checks={sha: one})
    assert ar.ci_state(sha) == 'pending'
    world.gh(checks={sha: one + [{'name': "Compile with Windhawk's compiler", 'status': 'completed',
                                  'conclusion': 'timed_out'}]})
    assert ar.ci_state(sha) == 'failure'
    world.gh(checks={sha: one + [{'name': 'lint', 'status': 'completed', 'conclusion': 'failure'}]})
    assert ar.ci_state(sha) == 'pending'


def test_draft_never_released(world, ar):
    world.pr(5, draft=True)
    assert ar.release_pending(set()) == []
    assert 'release create' not in world.log()


def test_fork_pr_ignored(world, ar):
    world.pr(5, cross=True)
    assert ar.pending_build_prs() == []
    assert ar.release_pending(set()) == []
    assert 'check-runs' not in world.log()


def test_ci_unreachable_leaves_pr(world, ar):
    world.pr(5)
    world.gh(fail=['api'])
    assert ar.release_pending(set()) == []
    assert world.notified() == ''
    world.gh(fail=['pr list'])
    assert ar.release_pending(set()) == []
    assert world.notified() == ''


def test_refused_release_toasts_once(world, ar):
    world.pr(5, signed=False)
    for _ in range(2):
        with pytest.raises(ar.ReleaseRefused):
            ar.release(ar.pending_build_prs()[0])
    assert ar.release_pending(set()) == []
    assert ar.release_pending(set()) == []
    assert world.notified().count('Taskbar 10.0.26100.9999: release refused') == 1
    assert 'not signed by this PC' in world.notified()


def test_autorelease_leaves_checkout_alone(world, ar):
    git(world.checkout, 'switch', '-q', '-c', 'wip')
    (world.checkout / 'README.md').write_text('owner edit\n')
    before = git(world.checkout, 'rev-parse', 'HEAD', '--abbrev-ref', 'HEAD')
    world.pr(5)
    v = world.next_version()
    assert ar.release_pending(set()) == [v]
    assert git(world.checkout, 'rev-parse', 'HEAD', '--abbrev-ref', 'HEAD') == before
    assert (world.checkout / 'README.md').read_text() == 'owner edit\n'
    assert git(world.checkout, 'tag', '-l', f'v{v}') == ''


def test_second_pr_reopened_after_release(world, tmp_path, msdl, dll_9549, dll_9807, pdb_9549):
    # A PR for another build is still open from an earlier run when 9807
    # arrives: the older one releases, 9807's PR is then stale (same version,
    # old main), so it is closed and opened again from the new main.
    world.pr(5)
    first = world.next_version()
    stage(tmp_path, dll_9549, 9549)
    sym = tmp_path / 'symbols/0A5DD95BEF53208B28B2F3D8586E64261'
    sym.mkdir(parents=True)
    os.symlink(pdb_9549, sym / 'Taskbar.pdb')
    env = {**os.environ, **world.env(msdl), 'TASKBAR_DLL': dll_9807, 'WINSXS': str(tmp_path / 'WinSxS'),
           'WINDHAWK_SYMBOLS': str(tmp_path / 'symbols'), 'TASKBAR_CACHE': str(tmp_path / 'cache')}
    p = subprocess.run([str(world.checkout / 'tools/watch-build.sh')], cwd=world.checkout,
                       capture_output=True, text=True, env=env)
    assert p.returncode == 0, p.stdout + p.stderr
    log = world.log()
    assert log.count('pr create') == 2
    assert (f'pr close 100 --delete-branch --comment Superseded by v{first}; '
            'the watcher reopens it from the new main.') in log
    major, minor, _ = first.split('.')
    second = f'{major}.{int(minor) + 1}.0'
    assert git(world.origin, 'cat-file', '-t', f'v{first}') == 'tag'
    # The reopened PR was opened on this run, so its CI was waited on and it released too.
    assert git(world.origin, 'cat-file', '-t', f'v{second}') == 'tag'
    assert f'pr comment 101 --body Released automatically in v{second}' in log
    assert git(world.origin, 'show', f'v{second}:{SRC}').count('10.0.26100.9807') >= 1
    assert f'v{first} released; install it in Windhawk by hand' in world.notified()
    assert f'v{second} released; install it in Windhawk by hand' in world.notified()


def test_watcher_exits_8_when_a_release_is_refused(world, ar, monkeypatch):
    import watch_build
    monkeypatch.setattr(watch_build, 'PE', lambda path: None)
    monkeypatch.setattr(watch_build, 'staged_build', lambda running: None)
    monkeypatch.setattr(watch_build, 'check_builds', lambda builds, dry_run: ([0], set()))
    world.pr(5, signed=False)
    assert watch_build.main([]) == 8
    assert watch_build.main(['--dry-run']) == 0


def run_watcher(world, tmp_path, msdl, dll_9549, dll_9807, pdb_9549):
    stage(tmp_path, dll_9549, 9549)
    sym = tmp_path / 'symbols/0A5DD95BEF53208B28B2F3D8586E64261'
    sym.mkdir(parents=True)
    os.symlink(pdb_9549, sym / 'Taskbar.pdb')
    env = {**os.environ, **world.env(msdl), 'TASKBAR_DLL': dll_9807, 'WINSXS': str(tmp_path / 'WinSxS'),
           'WINDHAWK_SYMBOLS': str(tmp_path / 'symbols'), 'TASKBAR_CACHE': str(tmp_path / 'cache')}
    return subprocess.run([str(world.checkout / 'tools/watch-build.sh')], cwd=world.checkout,
                          capture_output=True, text=True, env=env)


def test_draft_not_superseded(world, tmp_path, msdl, dll_9549, dll_9807, pdb_9549):
    # The owner made 9807's PR a draft to stop it. Another PR releasing must
    # not close it, or the watcher would reopen it as ready and release it.
    world.pr(5)
    first = world.next_version()
    world.pr(6, build='10.0.26100.9807', created='2026-02-01T00:00:00Z', draft=True)
    p = run_watcher(world, tmp_path, msdl, dll_9549, dll_9807, pdb_9549)
    assert p.returncode == 0, p.stdout + p.stderr
    assert git(world.origin, 'cat-file', '-t', f'v{first}') == 'tag'
    log = world.log()
    assert 'pr close 6' not in log
    assert 'pr create' not in log
    assert 'refs/heads/build/10.0.26100.9807' in git(world.origin, 'show-ref')


def test_release_failure_is_retried_not_refused(world, ar, monkeypatch):
    # gh fails after the tag is pushed: not a refusal. The next run resumes.
    import watch_build
    monkeypatch.setattr(watch_build, 'PE', lambda path: None)
    monkeypatch.setattr(watch_build, 'staged_build', lambda running: None)
    monkeypatch.setattr(watch_build, 'check_builds', lambda builds, dry_run: ([0], set()))
    world.pr(5)
    v = world.next_version()
    world.gh(fail=['release create'])
    assert watch_build.main([]) == 6
    assert watch_build.main([]) == 6
    notes = world.notified()
    assert 'release refused' not in notes
    assert notes.count('Taskbar 10.0.26100.9999: release failed, will retry') == 1
    assert git(world.origin, 'cat-file', '-t', f'v{v}') == 'tag'
    world.gh(fail=[])
    assert ar.release_pending(set()) == [v]
    assert f'v{v} released; install it in Windhawk by hand' in world.notified()


def test_moved_head_is_left_quietly(world, ar):
    world.pr(5)
    state = world.gh()
    state['prs'][0]['headRefOid'] = '1' * 40   # pushed over since `pr list`
    world.gh(prs=state['prs'])
    assert ar.release_pending(set()) == []
    assert world.notified() == ''
    assert 'pr view' not in world.log(), 'release.sh ran'


def test_release_step_has_a_deadline(world, ar, monkeypatch):
    a, b = world.pr(5), world.pr(6, build='10.0.26100.9998', created='2026-02-01T00:00:00Z')
    world.gh(checks={a: 'in_progress', b: 'in_progress'})
    monkeypatch.setenv('CI_WAIT', '5')
    monkeypatch.setenv('RELEASE_DEADLINE', '0.5')
    start = time.monotonic()
    assert ar.release_pending({5, 6}) == []
    assert time.monotonic() - start < 2, 'waited past the deadline'


def installs(ar, monkeypatch):
    """Record install() calls instead of installing."""
    calls = []
    monkeypatch.setattr(ar.windhawk_install, 'install', lambda version, source: calls.append((version, source)))
    return calls


def move_main(world):
    """Main moves for a reason of its own (a manual release, say)."""
    git(world.author, 'fetch', '-q', 'origin')
    git(world.author, 'switch', '-q', '-C', 'side', 'origin/main')
    (world.author / 'docs/note.txt').write_text('moved\n')
    git(world.author, 'add', 'docs/note.txt')
    git(world.author, 'commit', '-q', '-m', 'docs: a note')
    git(world.author, 'push', '-q', 'origin', 'HEAD:main')


def test_merged_pr_released_installed_and_others_superseded(world, ar, monkeypatch):
    # As on GitHub: pushing main marks the PR merged and `pr close` refuses.
    # The release is out by then, so it is installed and the rest superseded.
    world.gh(merge_on_push=True)
    calls = installs(ar, monkeypatch)
    world.pr(5)
    world.pr(6, build='10.0.26100.9998', created='2026-02-01T00:00:00Z')
    v = world.next_version()
    closed = []
    assert ar.release_pending(set(), closed) == [v]
    assert calls == [(v, git(world.origin, 'show', f'v{v}:{SRC}') + '\n')]
    log = world.log()
    assert f'pr comment 5 --body Released automatically in v{v}' in log
    assert 'pr close 5' not in log
    assert closed == [6]
    branches = git(world.origin, 'branch', '--list')
    assert 'build/10.0.26100.9999' not in branches and 'build/10.0.26100.9998' not in branches
    assert f'v{v} released and installed for taskbar 10.0.26100.9999' in world.notified()


def test_already_released_still_installs(world, ar, monkeypatch):
    # A run stopped after the release, before installing, and the PR could
    # not be closed: the next run finds the release, installs it and
    # closes the stale PRs.
    world.pr(5)
    world.pr(6, build='10.0.26100.9998', created='2026-02-01T00:00:00Z')
    v = world.next_version()
    world.gh(fail=['pr close'])

    def crash(version, source):
        raise KeyboardInterrupt
    monkeypatch.setattr(ar.windhawk_install, 'install', crash)
    with pytest.raises(KeyboardInterrupt):
        ar.release_pending(set())
    world.gh(fail=[])
    calls = installs(ar, monkeypatch)
    closed = []
    assert ar.release_pending(set(), closed) == [v]
    assert calls == [(v, git(world.origin, 'show', f'v{v}:{SRC}') + '\n')]
    assert world.log().count('release create') == 1
    assert closed == [6]
    assert 'release refused' not in world.notified()


def test_release_failure_ends_the_round(world, ar):
    # A pushed main and v1.x.0, then failed to publish. B bumps to the same
    # version: it must not resume A's release with its own notes.
    world.pr(5)
    world.pr(6, build='10.0.26100.9998', created='2026-02-01T00:00:00Z')
    world.gh(fail=['release create'])
    failed = []
    assert ar.release_pending(set(), failed=failed) == []
    assert failed == [5]
    assert world.log().count('release create') == 1
    assert 'pr close 6' not in world.log()


def test_stale_pr_closed_quietly(world, ar):
    world.pr(5)
    world.pr(6, build='10.0.26100.9998', created='2026-02-01T00:00:00Z', draft=True)
    move_main(world)
    closed, refused = [], []
    assert ar.release_pending(set(), closed, refused) == []
    log = world.log()
    assert 'pr close 5 --delete-branch --comment Main moved since this PR was cut' in log
    assert closed == [5] and refused == []
    assert world.notified() == ''
    assert 'pr close 6' not in log, 'draft closed'
    assert 'release create' not in log


def test_stale_pr_reopened_from_new_main(world, tmp_path, msdl, dll_9549, dll_9807, pdb_9549):
    world.pr(5, build='10.0.26100.9807')
    move_main(world)
    v = world.next_version()
    p = run_watcher(world, tmp_path, msdl, dll_9549, dll_9807, pdb_9549)
    assert p.returncode == 0, p.stdout + p.stderr
    log = world.log()
    assert 'pr close 5 --delete-branch' in log
    assert log.count('pr create') == 1
    assert git(world.origin, 'cat-file', '-t', f'v{v}') == 'tag'
    assert 'release refused' not in world.notified()


def test_stray_local_tags_do_not_reach_the_release(world, ar):
    # The owner's checkout has a tag of the next version and one that clashes
    # with origin's: neither may reach the release clone.
    world.pr(5)
    v = world.next_version()
    git(world.checkout, 'update-ref', f'refs/tags/v{v}', 'HEAD')
    old = git(world.checkout, 'tag', '-l', 'v1.1*').split()[0]
    git(world.checkout, 'update-ref', f'refs/tags/{old}', 'HEAD')
    assert ar.release_pending(set()) == [v]
    assert git(world.origin, 'cat-file', '-t', f'v{v}') == 'tag'
    assert git(world.checkout, 'rev-parse', old) == git(world.checkout, 'rev-parse', 'HEAD'), 'checkout tag touched'


def test_pr_made_draft_after_listing_is_skipped_quietly(world, ar, capsys):
    world.pr(5)
    world.gh(view={'5': {'isDraft': True}})
    refused = []
    assert ar.release_pending(set(), refused=refused) == []
    assert refused == []
    assert world.notified() == ''
    assert 'became a draft' in capsys.readouterr().out
    assert 'release create' not in world.log()
