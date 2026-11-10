"""The watcher runs unattended: a network that silently drops SSH to GitHub
must make its git calls fail within seconds, not hang until the scheduled
task's 30-minute limit."""
import os
import shlex

import watch_build


def test_git_ssh_gets_timeouts(monkeypatch):
    monkeypatch.delenv('GIT_SSH_COMMAND', raising=False)
    watch_build.bound_ssh()
    argv = shlex.split(os.environ['GIT_SSH_COMMAND'])
    assert argv[0] == 'ssh'
    opts = {argv[i + 1] for i, a in enumerate(argv) if a == '-o'}
    assert 'ConnectTimeout=20' in opts
    assert 'ServerAliveInterval=15' in opts
    assert 'ServerAliveCountMax=4' in opts


def test_owner_ssh_command_is_kept(monkeypatch):
    monkeypatch.setenv('GIT_SSH_COMMAND', 'ssh -i /some/key')
    watch_build.bound_ssh()
    assert os.environ['GIT_SSH_COMMAND'] == 'ssh -i /some/key'
