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
    """A clone of this repo with the 9807 entry taken out, as if it were new."""
    clone = tmp_path / 'repo'
    clone_working_copy(clone)
    src = clone / 'src/taskbar-autohide-any-edge.wh.cpp'
    text = src.read_text()
    src.write_text(re.sub(r'    // Matched against 10\.0\.26100\.9549.*?0x35C40\},\n', '', text, flags=re.S))
    return clone


def clone_working_copy(dest):
    """Clone for git state (origin, branches), then overlay the working tree's
    tools and PR template so the code under test is the code being edited."""
    subprocess.run(['git', 'clone', '-q', REPO, str(dest)], check=True)
    for d in ('tools', '.github'):
        shutil.copytree(os.path.join(REPO, d), dest / d, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('__pycache__'))
    os.symlink(os.path.join(REPO, '.venv'), dest / '.venv')


def run(cwd, dll, msdl_base, *args, env=None):
    notes = cwd / 'notify.log' if hasattr(cwd, 'joinpath') else None
    e = {**os.environ, 'TASKBAR_DLL': dll, 'MSDL_BASE': msdl_base,
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
    p, notes = run(checkout, dll_9807, msdl, env={'SSH_AUTH_SOCK': str(checkout / 'no-agent.sock')})
    assert p.returncode == 4, p.stdout + p.stderr
    assert 'signing is unavailable' in notes
    after = subprocess.run(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout
    assert after == head
    assert 'build/' not in subprocess.run(['git', '-C', str(checkout), 'branch'], capture_output=True, text=True).stdout
