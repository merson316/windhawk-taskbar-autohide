import os

import pytest

from matcher import reference
from matcher.pe import PE

WINSXS = '/mnt/c/Windows/WinSxS'


def test_unreadable_candidate_is_skipped(tmp_path, monkeypatch, dll_9278, dll_9549, dll_9807):
    for n, dll in (('a', dll_9278), ('b', dll_9549)):
        d = tmp_path / f'amd64_microsoft-windows-taskbar-dll_{n}'
        d.mkdir()
        os.symlink(dll, d / 'Taskbar.dll')
    bad = tmp_path / 'amd64_microsoft-windows-taskbar-dll_c'
    bad.mkdir()
    (bad / 'Taskbar.dll').write_bytes(b'not a PE file')
    monkeypatch.setattr(reference, 'WINSXS', str(tmp_path))
    ref, _ = reference.auto_reference(PE(dll_9807))
    assert ref.file_version() == '10.0.26100.9549'


@pytest.fixture
def stub(tmp_path, monkeypatch):
    """A symbol server answering every request with `stub.body`."""
    import http.server
    import threading
    state = {'body': b''}

    class H(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200)
            self.end_headers()
            self.wfile.write(state['body'])

        def log_message(self, *a):
            pass
    srv = http.server.HTTPServer(('127.0.0.1', 0), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    monkeypatch.setattr(reference, 'MSDL', f'http://127.0.0.1:{srv.server_port}')
    monkeypatch.setattr(reference, 'SYMBOLS', str(tmp_path / 'windhawk'))
    monkeypatch.setattr(reference, 'CACHE', str(tmp_path / 'cache'))
    yield state
    srv.shutdown()


def test_bad_pdb_download_is_not_cached(stub, tmp_path):
    stub['body'] = b'<html>error page</html>'
    with pytest.raises(ValueError, match='not a PDB'):
        reference.fetch_pdb('ABC1')
    assert not (tmp_path / 'cache/ABC1/Taskbar.pdb').exists()


def test_poisoned_pdb_cache_is_replaced(stub, tmp_path, pdb_9549):
    poisoned = tmp_path / 'cache/ABC1/Taskbar.pdb'
    poisoned.parent.mkdir(parents=True)
    poisoned.write_bytes(b'<html>partial')
    stub['body'] = open(pdb_9549, 'rb').read()
    path = reference.fetch_pdb('ABC1')
    assert open(path, 'rb').read(len(reference.PDB_MAGIC)) == reference.PDB_MAGIC


def test_bad_dll_download_is_not_cached(stub, tmp_path):
    stub['body'] = b'<html>error page</html>'
    with pytest.raises(ValueError, match='not a PE'):
        reference.fetch_dll(0x1234, 0x5000)
    assert not list((tmp_path / 'cache').rglob('taskbar.dll'))
