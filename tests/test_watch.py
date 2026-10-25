import http.server
import os
import re
import shutil
import subprocess
import threading

import pytest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))


@pytest.fixture
def msdl(request):
    status = getattr(request, 'param', 404)

    class H(http.server.BaseHTTPRequestHandler):
        def _reply(self):
            self.send_response(status)
            if status == 302:
                self.send_header('Location', 'https://example.invalid/pdb')
            self.end_headers()
        do_HEAD = do_GET = _reply

        def log_message(self, *a):
            pass

    srv = http.server.HTTPServer(('127.0.0.1', 0), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    yield f'http://127.0.0.1:{srv.server_port}'
    srv.shutdown()


@pytest.fixture
def checkout(tmp_path):
    """The owner's checkout of a repo whose origin/main lacks 9807, as if it
    were new. origin is a throwaway bare repo."""
    origin = tmp_path / 'origin.git'
    subprocess.run(['git', 'clone', '-q', '--bare', REPO, str(origin)], check=True)
    author = tmp_path / 'author'
    subprocess.run(['git', 'clone', '-q', str(origin), str(author)], check=True)
    src = author / 'src/taskbar-autohide-any-edge.wh.cpp'
    src.write_text(re.sub(r'    // Matched against 10\.0\.26100\.9549.*?0x35C40\},\n', '', src.read_text(), flags=re.S))
    subprocess.run(['git', '-C', str(author), 'commit', '-qam', 'test: drop 9807'], check=True)
    subprocess.run(['git', '-C', str(author), 'push', '-q', 'origin', 'HEAD:main'], check=True)
    clone = tmp_path / 'repo'
    subprocess.run(['git', 'clone', '-q', str(origin), str(clone)], check=True)
    overlay_tools(clone)
    return clone


def overlay_tools(dest):
    """Overlay the working tree's tools and PR template so the code under test
    is the code being edited, without making the checkout dirty in git's eyes."""
    for d in ('tools', '.github'):
        shutil.copytree(os.path.join(REPO, d), dest / d, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('__pycache__'))
    files = subprocess.run(['git', '-C', str(dest), 'ls-files', 'tools', '.github'],
                           capture_output=True, text=True, check=True).stdout.split()
    subprocess.run(['git', '-C', str(dest), 'update-index', '--assume-unchanged', *files], check=True)
    os.symlink(os.path.join(REPO, '.venv'), dest / '.venv')


def clone_working_copy(dest):
    subprocess.run(['git', 'clone', '-q', REPO, str(dest)], check=True)
    overlay_tools(dest)


def run(cwd, dll, msdl_base, *args, env=None, gh_exit=0):
    notes = cwd.parent / 'notify.log'
    bin_dir = cwd.parent / 'bin'
    bin_dir.mkdir(exist_ok=True)
    (bin_dir / 'gh').write_text(f'#!/bin/sh\necho https://github.com/example/repo/pull/42\nexit {gh_exit}\n')
    (bin_dir / 'gh').chmod(0o755)
    e = {**os.environ, 'TASKBAR_DLL': dll, 'MSDL_BASE': msdl_base, 'PATH': f'{bin_dir}:{os.environ["PATH"]}',
         'NOTIFY_CMD': f'sh -c \'echo "$*" >> {notes}\' notify', **(env or {})}
    p = subprocess.run([os.path.join(str(cwd), 'tools/watch-build.sh'), *args], cwd=cwd,
                       capture_output=True, text=True, env=e)
    return p, (notes.read_text() if notes and notes.exists() else '')


def test_watch_known_build(tmp_path, dll_9807, msdl):
    repo = tmp_path / 'r'
    clone_working_copy(repo)
    p, _ = run(repo, dll_9807, msdl)
    assert p.returncode == 0, p.stderr
    assert 'known: 10.0.26100.9807' in p.stdout


@pytest.mark.parametrize('msdl', [302], indirect=True)
def test_watch_skips_when_pdb_published(checkout, dll_9807, msdl):
    p, notes = run(checkout, dll_9807, msdl)
    assert p.returncode == 0, p.stderr
    assert 'symbols published' in p.stdout
    assert notes == ''


def test_watch_dry_run_new_build(checkout, dll_9807, msdl):
    p, _ = run(checkout, dll_9807, msdl, '--dry-run')
    assert p.returncode == 0, p.stderr
    assert '0x65250, 0x41F70, 0x3CC90, 0x41ED0, 0x5E670, 0x41260, 0x35C40' in p.stdout
    assert 'would open draft PR' in p.stdout


def test_watch_stops_without_signing(checkout, dll_9807, msdl):
    head = subprocess.run(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout
    # Simulate the key agent not being loaded: the configured signer fails.
    no_signer = {'GIT_CONFIG_COUNT': '1', 'GIT_CONFIG_KEY_0': 'gpg.ssh.program', 'GIT_CONFIG_VALUE_0': '/bin/false'}
    p, notes = run(checkout, dll_9807, msdl, env=no_signer)
    assert p.returncode == 4, p.stdout + p.stderr
    assert 'signing is unavailable' in notes
    after = subprocess.run(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout
    assert after == head
    assert 'build/' not in subprocess.run(['git', '-C', str(checkout), 'branch'], capture_output=True, text=True).stdout


def remote_branches(repo):
    return subprocess.run(['git', '-C', str(repo), 'ls-remote', '--heads', 'origin'],
                          capture_output=True, text=True, check=True).stdout


def test_watch_opens_pr_without_touching_checkout(checkout, dll_9807, msdl):
    (checkout / 'src/taskbar-autohide-any-edge.wh.cpp').write_text('// half-done owner edit\n')
    before = subprocess.run(['git', '-C', str(checkout), 'rev-parse', 'HEAD', '--abbrev-ref', 'HEAD'],
                            capture_output=True, text=True).stdout
    p, notes = run(checkout, dll_9807, msdl)
    assert p.returncode == 0, p.stdout + p.stderr
    assert 'draft PR ready' in notes
    after = subprocess.run(['git', '-C', str(checkout), 'rev-parse', 'HEAD', '--abbrev-ref', 'HEAD'],
                           capture_output=True, text=True).stdout
    assert after == before
    assert (checkout / 'src/taskbar-autohide-any-edge.wh.cpp').read_text() == '// half-done owner edit\n'
    assert 'refs/heads/build/10.0.26100.9807' in remote_branches(checkout)
    subprocess.run(['git', '-C', str(checkout), 'fetch', '-q', 'origin'], check=True)
    changed = subprocess.run(['git', '-C', str(checkout), 'diff', '--name-only', 'origin/main',
                              'origin/build/10.0.26100.9807'], capture_output=True, text=True).stdout.split()
    assert changed == ['README.md', 'src/taskbar-autohide-any-edge.wh.cpp']
    assert 'build/' not in subprocess.run(['git', '-C', str(checkout), 'branch'], capture_output=True, text=True).stdout


def test_watch_failure_notifies_and_cleans_up(checkout, dll_9807, msdl):
    p, notes = run(checkout, dll_9807, msdl, gh_exit=1)
    assert p.returncode not in (0, 3, 4, 5)
    assert 'watcher failed' in notes
    assert 'build/' not in remote_branches(checkout)
    assert 'build/' not in subprocess.run(['git', '-C', str(checkout), 'branch'], capture_output=True, text=True).stdout
    # and the next sign-in tries again rather than reporting known or pending
    p2, _ = run(checkout, dll_9807, msdl, '--dry-run')
    assert 'would open draft PR' in p2.stdout
