#!/usr/bin/env python3
"""A scripted stand-in for the gh CLI. State lives in the JSON file $GH_STATE:

  prs      open PRs: number, headRefName, headRefOid, createdAt, isDraft,
           isCrossRepository, baseRefName, body
  checks   sha -> a conclusion ('success', 'failure', 'in_progress') or a
           list of check runs; any other sha gets `ci` ('success' by default)
  fail     command prefixes ('api', 'pr list', ...) that exit `fail_code`
           (default 1), as when GitHub cannot be reached (gh exits 4 when
           not logged in)
  next     the number the next `pr create` gets
  merge_on_push  as GitHub does: once origin's main contains a PR's head,
           the PR is MERGED, is no longer listed as open, and `pr close`
           refuses it
  view     number -> fields `pr view` reports over the listed ones (a PR
           made a draft after `pr list`, say)
  releases tags released so far (`release create` adds, `release view` reads)

The origin is the bare repo $GH_ORIGIN: `pr create` points refs/pull/N/head
at the pushed branch, `pr close --delete-branch` deletes the branch. Every
call is logged, one per line, to $GH_LOG."""
import datetime
import json
import os
import subprocess
import sys

REQUIRED = ('Python tests', "Compile with Windhawk's compiler")


def runs(conclusion):
    if conclusion == 'in_progress':
        return [{'name': n, 'status': 'in_progress', 'conclusion': None} for n in REQUIRED]
    return [{'name': n, 'status': 'completed', 'conclusion': conclusion} for n in REQUIRED]


def opt(args, name):
    return args[args.index(name) + 1] if name in args else None


def origin(*args):
    return subprocess.run(['git', '--git-dir', os.environ['GH_ORIGIN'], *args],
                          check=True, capture_output=True, text=True).stdout.strip()


def merged(state, pr):
    if not state.get('merge_on_push'):
        return False
    return subprocess.run(['git', '--git-dir', os.environ['GH_ORIGIN'], 'merge-base', '--is-ancestor',
                           pr['headRefOid'], 'refs/heads/main'], capture_output=True).returncode == 0


def main(args):
    with open(os.environ['GH_LOG'], 'a') as f:
        f.write(' '.join(args) + '\n')
    path = os.environ['GH_STATE']
    state = json.load(open(path))
    cmd = ' '.join(args[:2])
    if any(cmd.startswith(p) for p in state.get('fail', [])):
        print('error connecting to api.github.com', file=sys.stderr)
        return state.get('fail_code', 1)
    prs = state.setdefault('prs', [])
    closed = state.setdefault('closed', [])
    if cmd == 'pr list':
        print(json.dumps([p for p in prs if not merged(state, p)]))
    elif cmd == 'pr view':
        n = int(args[2])
        pr = next((p for p in prs if p['number'] == n), None)
        pr_state = 'MERGED' if pr and merged(state, pr) else 'OPEN' if pr else 'CLOSED'
        pr = pr or next(p for p in closed if p['number'] == n)
        print(json.dumps({**pr, 'state': pr_state, 'url': f'https://github.com/example/repo/pull/{n}',
                          **state.get('view', {}).get(str(n), {})}))
    elif cmd == 'pr comment':
        pass
    elif cmd == 'pr create':
        n = state.get('next', 100)
        state['next'] = n + 1
        branch = opt(args, '--head')
        sha = origin('rev-parse', f'refs/heads/{branch}')
        origin('update-ref', f'refs/pull/{n}/head', sha)
        prs.append({'number': n, 'headRefName': branch, 'headRefOid': sha,
                    'createdAt': datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
                    'isDraft': '--draft' in args, 'isCrossRepository': False, 'baseRefName': 'main', 'body': ''})
        print(f'https://github.com/example/repo/pull/{n}')
    elif cmd == 'pr close':
        pr = next(p for p in prs if p['number'] == int(args[2]))
        if merged(state, pr):
            print(f'! Pull request #{pr["number"]} can\'t be closed because it was already merged', file=sys.stderr)
            return 1
        prs.remove(pr)
        closed.append(pr)
        if '--delete-branch' in args:
            origin('update-ref', '-d', f'refs/heads/{pr["headRefName"]}')
    elif cmd.startswith('api ') and args[1].endswith('/check-runs'):
        sha = args[1].split('/')[-2]
        ci = state.get('checks', {}).get(sha, state.get('ci', 'success'))
        print(json.dumps({'check_runs': ci if isinstance(ci, list) else runs(ci)}))
    elif cmd == 'release create':
        state.setdefault('releases', []).append(args[2])
        print(f'https://github.com/example/repo/releases/tag/{args[2]}')
    elif cmd == 'release view':
        if args[2] not in state.get('releases', []):
            print('release not found', file=sys.stderr)
            return 1
        print(json.dumps({'url': f'https://github.com/example/repo/releases/tag/{args[2]}', 'isDraft': False}))
    with open(path, 'w') as f:
        json.dump(state, f)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
