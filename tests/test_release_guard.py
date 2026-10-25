"""release.sh --pr must refuse a PR that could run code with the signing key
loaded: one from a fork, or one touching anything but src/ and README.md."""
import json
import os
import subprocess

import pytest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))


def run_release(tmp_path, pr_json):
    clone = tmp_path / 'repo'
    subprocess.run(['git', 'clone', '-q', REPO, str(clone)], check=True)
    subprocess.run(['cp', os.path.join(REPO, 'tools/release.sh'), clone / 'tools/release.sh'], check=True)
    # The script under test is the edited working copy; keep the clone clean.
    subprocess.run(['git', '-C', str(clone), 'update-index', '--assume-unchanged', 'tools/release.sh'], check=True)
    bin_dir = tmp_path / 'bin'
    bin_dir.mkdir()
    (bin_dir / 'gh').write_text(f"#!/bin/sh\ncat <<'JSON'\n{json.dumps(pr_json)}\nJSON\n")
    (bin_dir / 'gh').chmod(0o755)
    env = {**os.environ, 'PATH': f'{bin_dir}:{os.environ["PATH"]}'}
    return subprocess.run(['tools/release.sh', '9.9.9', '--pr', '7'], cwd=clone, env=env,
                          capture_output=True, text=True, input='')


@pytest.mark.parametrize('pr,message', [
    ({'isCrossRepository': True, 'headRefOid': 'a' * 40, 'headRefName': 'build/x',
      'files': [{'path': 'src/taskbar-autohide-any-edge.wh.cpp'}], 'body': ''}, 'from a fork'),
    ({'isCrossRepository': False, 'headRefOid': 'a' * 40, 'headRefName': 'build/x',
      'files': [{'path': 'src/taskbar-autohide-any-edge.wh.cpp'}, {'path': 'tools/release.sh'}], 'body': ''},
     'tools/release.sh'),
])
def test_release_refuses_unsafe_pr(tmp_path, pr, message):
    p = run_release(tmp_path, pr)
    assert p.returncode != 0
    assert message in p.stderr
