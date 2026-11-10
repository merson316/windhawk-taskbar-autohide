"""Releases the watcher's own build PRs once CI has passed on their exact head
commit, by running `tools/release.sh --auto` in a throwaway clone (the
owner's checkout is never touched). Oldest first, one release per round:
every build PR bumps to the same next version from the same main, so once one
releases the others are stale and are closed for the watcher to reopen from
the new main.
After a release, the new version is installed in the owner's Windhawk (see
windhawk_install.py).

A PR whose head does not contain origin's main (main moved for some other
reason: a manual release, a stale PR that could not be closed) cannot pass
the trust check as it is; it is closed the same way, with no toast, for the
watcher to reopen from the new main. Drafts are never closed.

A gh or git network error while listing PRs or reading CI is printed and
skipped: the next run tries again. So is a PR whose head moved or that became
a draft since it was listed. A release that failed for any reason other than
release.sh refusing it toasts once a day and ends the step: every build PR
bumps to the same version, so the next PR must not build on a half-done
release. The whole step stops waiting after RELEASE_DEADLINE seconds
(default 1200), so a run stays inside the scheduled task's limit."""
import datetime
import json
import os
import re
import shutil
import subprocess
import tempfile
import time
from collections import namedtuple

import watch_build as wb
import windhawk_install

BuildPR = namedtuple('BuildPR', 'number branch sha created draft')
REQUIRED = ('Python tests', "Compile with Windhawk's compiler")
FAILED = ('failure', 'cancelled', 'timed_out')


class ReleaseRefused(Exception):
    """release.sh refused the PR (exit 3 under --auto); the message is its
    stderr. The PR will not release as it is."""


class ReleaseFailed(Exception):
    """The release failed for another reason (network, signing key not
    loaded, ...); a later run may get past it."""


class Moved(Exception):
    """The PR's head is no longer the commit CI was read on."""


class Drafted(Moved):
    """The PR became a draft after it was listed (release.sh exit 10)."""


class Unpublished(Exception):
    """release.sh pushed main and the tag but could not publish the release
    (exit 11). GitHub has marked the PR merged, so no later run lists it:
    the owner finishes it with the command in the message."""

    def __init__(self, version, message):
        super().__init__(message)
        self.version = version


class Stale(Exception):
    """The PR's head does not contain origin's main: main moved since the
    watcher cut it, so it cannot release as it is."""


class Unreachable(Exception):
    """gh could not answer, or answered something that is not JSON."""


def _gh_json(*args):
    r = subprocess.run(['gh', *args], capture_output=True, text=True, cwd=wb.REPO)
    if r.returncode != 0:
        raise Unreachable(f'gh {args[0]} {args[1]}: {r.stderr.strip() or f"exit {r.returncode}"}')
    try:
        return json.loads(r.stdout)
    except ValueError:
        raise Unreachable(f'gh {args[0]} {args[1]}: not JSON: {r.stdout.strip()[:200]}') from None


def pending_build_prs():
    """Open same-repo PRs from a build/ branch, oldest first."""
    prs = _gh_json('pr', 'list', '--state', 'open', '--json',
                   'number,headRefName,headRefOid,createdAt,isDraft,isCrossRepository')
    return sorted((BuildPR(p['number'], p['headRefName'], p['headRefOid'], p['createdAt'], p['isDraft'])
                   for p in prs if not p['isCrossRepository'] and p['headRefName'].startswith('build/')),
                  key=lambda p: p.created)


def ci_state(sha):
    """'success' once both required checks succeeded on sha, 'failure' once
    either failed, else 'pending' (a missing check counts as pending)."""
    runs = _gh_json('api', f'repos/{{owner}}/{{repo}}/commits/{sha}/check-runs')['check_runs']
    latest = {}
    for run in runs:   # a re-run adds a run with the same name and a larger id
        if run['name'] in REQUIRED and run.get('id', 0) >= latest.get(run['name'], {}).get('id', 0):
            latest[run['name']] = run
    states = [latest.get(n) for n in REQUIRED]
    if any(r and r['status'] == 'completed' and r['conclusion'] in FAILED for r in states):
        return 'failure'
    if all(r and r['status'] == 'completed' and r['conclusion'] == 'success' for r in states):
        return 'success'
    return 'pending'


def wait_for_ci(sha, timeout):
    every = float(os.environ.get('CI_EVERY', 20))
    deadline = time.monotonic() + timeout
    while True:
        state = ci_state(sha)
        if state != 'pending' or time.monotonic() >= deadline:
            return state
        time.sleep(every)


def _release(pr):
    """Run release.sh --auto for pr in a fresh clone; return (version, url,
    the released source as text)."""
    clone = tempfile.mkdtemp(prefix='taskbar-release-')
    try:
        return _release_in(clone, pr)
    except subprocess.CalledProcessError as e:   # git, before release.sh runs
        raise ReleaseFailed((e.stderr or '').strip() or f'git exited {e.returncode}') from None
    finally:
        shutil.rmtree(clone, ignore_errors=True)


def _release_in(clone, pr):
    def g(*args):
        return wb.git('-C', clone, *args).stdout.strip()
    # --no-tags: the owner's local tags (a stray v1.x, a tag named main) must
    # not reach release.sh or clash with origin's in the fetch below.
    wb.git('clone', '-q', '--no-checkout', '--no-tags', wb.REPO, clone)
    g('remote', 'set-url', 'origin', wb.git('-C', wb.REPO, 'remote', 'get-url', 'origin').stdout.strip())
    for key in ('user.name', 'user.email'):   # commit as the owner's checkout does
        value = wb.git('-C', wb.REPO, 'config', key, check=False).stdout.strip()
        if value:
            g('config', key, value)
    # release.sh, and the tools it runs, come from origin's main, not
    # from whatever the owner's checkout has. Tags too: release.sh resumes
    # from its tag if an earlier run pushed it.
    g('fetch', '-q', '--tags', 'origin', '+refs/heads/main:refs/remotes/origin/main',
      f'+refs/pull/{pr.number}/head:refs/remotes/origin/pr')
    g('checkout', '-q', '-B', 'main', 'origin/main')
    os.symlink(os.path.join(wb.REPO, '.venv'), os.path.join(clone, '.venv'))
    with open(os.path.join(clone, '.git/info/exclude'), 'a') as f:
        f.write('.venv\n')
    if g('rev-parse', 'refs/remotes/origin/pr') != pr.sha:
        raise Moved(f'PR #{pr.number} moved: its head is no longer {pr.sha[:12]}')

    def ancestor(a, b):
        return wb.git('-C', clone, 'merge-base', '--is-ancestor', a, b, check=False).returncode == 0
    # Already on main is not stale: an earlier run pushed it and stopped.
    if not ancestor('origin/main', pr.sha) and not ancestor(pr.sha, 'origin/main'):
        raise Stale(f'PR #{pr.number} is not based on the current main')
    # The version is the PR's own @version, read at the commit CI passed on.
    src = wb.git('-C', clone, 'show', f'{pr.sha}:{wb.SRC}', check=False)
    m = re.search(r'(?m)^// @version\s+(\S+)$', src.stdout)
    if not m:
        raise ReleaseRefused(f'PR #{pr.number} has no @version at {pr.sha[:12]}')
    version = m[1]
    r = subprocess.run(['tools/release.sh', version, '--pr', str(pr.number), '--auto', '--sha', pr.sha],
                       cwd=clone, capture_output=True, text=True, stdin=subprocess.DEVNULL)
    print(r.stdout, end='')
    print(r.stderr, end='')
    if r.returncode == 3:
        raise ReleaseRefused(r.stderr.strip())
    if r.returncode == 11:
        line = next((x for x in reversed(r.stderr.splitlines()) if 'tagged but not published' in x),
                    f'v{version} is tagged but not published')
        raise Unpublished(version, line.strip())
    if r.returncode == 10:
        raise Drafted(f'PR #{pr.number} became a draft; not releasing it')
    if r.returncode != 0:
        raise ReleaseFailed(r.stderr.strip() or f'release.sh exited {r.returncode}')
    url = next((line for line in reversed(r.stdout.splitlines()) if '/releases/tag/' in line), f'v{version}')
    # What is installed is what was tagged, read here before the clone goes.
    source = wb.git('-C', clone, 'show', f'refs/tags/v{version}:{wb.SRC}').stdout
    return version, url.strip(), source


def release(pr):
    """Release pr; return the version released. Raises ReleaseRefused."""
    return _release(pr)[0]


def release_deadline():
    return time.monotonic() + float(os.environ.get('RELEASE_DEADLINE', 1200))


def _first_line(text):
    lines = text.splitlines() or ['no reason given']
    # "PR #N refused:" says nothing on its own; the first problem follows it.
    return f'{lines[0]} {lines[1].strip()}' if lines[0].endswith(':') and len(lines) > 1 else lines[0]


def release_pending(opened, closed=None, refused=None, failed=None, deadline=None):
    """Release the oldest build PR whose CI passed; return the versions
    released. PRs in `opened` were opened on this run: wait for their CI,
    but not past `deadline` (time.monotonic(); default RELEASE_DEADLINE
    from now). The numbers of PRs closed as stale after a release, refused
    by release.sh, or whose release failed go into the lists `closed`,
    `refused` and `failed`, if given."""
    if deadline is None:
        deadline = release_deadline()
    try:
        prs = pending_build_prs()
    except Unreachable as e:
        print(f'cannot list PRs, trying next time: {e}')
        return []
    for pr in prs:
        build = pr.branch[len('build/'):]
        left = deadline - time.monotonic()
        if left <= 0:
            print('release step out of time; the other PRs wait for the next run')
            break
        if pr.draft:
            print(f'draft, not releasing: PR #{pr.number} ({build})')
            continue
        try:
            state = (wait_for_ci(pr.sha, min(float(os.environ.get('CI_WAIT', 900)), left))
                     if pr.number in opened else ci_state(pr.sha))
        except Unreachable as e:
            print(f'cannot read CI for PR #{pr.number}, trying next time: {e}')
            continue
        if state == 'pending':
            print(f'CI not finished, trying next time: PR #{pr.number} ({build})')
            continue
        if state == 'failure':
            print(f'CI failed: PR #{pr.number} ({build})')
            wb.notify_once(f'ci:{pr.sha}', f'Taskbar {build}: CI failed', _pr_url(pr))
            continue
        try:
            version, release_url, source = _release(pr)
        except Moved as e:
            print(f'{e}; trying next time')
            continue
        except Stale as e:
            print(f'{e}; closing it for the watcher to reopen')
            _close_stale(pr, 'Main moved since this PR was cut; the watcher reopens it from the new main.', closed)
            continue
        except ReleaseRefused as e:
            print(f'release refused: PR #{pr.number} ({build}): {e}')
            wb.notify_once(f'refused:{pr.sha}', f'Taskbar {build}: release refused', _first_line(str(e)))
            if refused is not None:
                refused.append(pr.number)
            continue
        except Unpublished as e:
            print(f'{e}')
            wb.notify_once(f'unpublished:{e.version}', f'v{e.version} tagged but not published', str(e))
            if failed is not None:
                failed.append(pr.number)
            break
        except ReleaseFailed as e:
            print(f'release failed, trying next time: PR #{pr.number} ({build}): {e}')
            wb.notify_once(f'releasefail:{pr.sha}:{datetime.date.today()}',
                           f'Taskbar {build}: release failed, will retry', _first_line(str(e)))
            if failed is not None:
                failed.append(pr.number)
            # The next PR bumps to the same version: it must not resume
            # this one's half-done release.
            break
        # The release is done either way; only the toast differs.
        try:
            windhawk_install.install(version, source)
        except windhawk_install.InstallError as e:
            print(f'v{version} released, not installed: {e}')
            wb.notify_once(f'install:{version}', f'v{version} released; install it in Windhawk by hand', str(e))
        else:
            wb.notify(f'v{version} released and installed for taskbar {build}', release_url)
        _supersede(pr, version, closed)
        return [version]
    return []


def _pr_url(pr):
    try:
        return _gh_json('pr', 'view', str(pr.number), '--json', 'url')['url']
    except (Unreachable, KeyError, TypeError):
        return f'PR #{pr.number}'


def _supersede(released, version, closed):
    try:
        # A draft is the owner's stop signal: closing it would let the
        # watcher reopen it as ready, and release it.
        others = [p for p in pending_build_prs() if p.number != released.number and not p.draft]
    except Unreachable as e:
        print(f'cannot list PRs to close the stale ones, trying next time: {e}')
        return
    for pr in others:
        _close_stale(pr, f'Superseded by v{version}; the watcher reopens it from the new main.', closed)


def _close_stale(pr, comment, closed):
    """Close pr and delete its branch, so the watcher matches its build
    again from the new main."""
    r = subprocess.run(['gh', 'pr', 'close', str(pr.number), '--delete-branch', '--comment', comment],
                       capture_output=True, text=True, cwd=wb.REPO)
    if r.returncode != 0:
        print(f'could not close stale PR #{pr.number}: {r.stderr.strip()}')
        return
    print(f'closed stale PR #{pr.number} ({pr.branch})')
    if closed is not None:
        closed.append(pr.number)
