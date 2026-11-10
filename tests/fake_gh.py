#!/usr/bin/env python3
"""A scripted stand-in for the gh CLI. State lives in the JSON file $GH_STATE:

  prs      open PRs: number, headRefName, headRefOid, createdAt, isDraft,
           isCrossRepository, baseRefName, body
  checks   sha -> a conclusion ('success', 'failure', 'in_progress') or a
           list of check runs; any other sha gets `ci` ('success' by default)
  fail     command prefixes ('api', 'pr list', ...) that exit 1, as when
           GitHub cannot be reached
  next     the number the next `pr create` gets

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


def main(args):
    with open(os.environ['GH_LOG'], 'a') as f:
        f.write(' '.join(args) + '\n')
    path = os.environ['GH_STATE']
    state = json.load(open(path))
    cmd = ' '.join(args[:2])
    if any(cmd.startswith(p) for p in state.get('fail', [])):
        print('error connecting to api.github.com', file=sys.stderr)
        return 1
    prs = state.setdefault('prs', [])
    if cmd == 'pr list':
        print(json.dumps(prs))
    elif cmd == 'pr view':
        pr = next(p for p in prs if p['number'] == int(args[2]))
        print(json.dumps({**pr, 'url': f'https://github.com/example/repo/pull/{pr["number"]}'}))
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
        prs.remove(pr)
        if '--delete-branch' in args:
            origin('update-ref', '-d', f'refs/heads/{pr["headRefName"]}')
    elif cmd.startswith('api ') and args[1].endswith('/check-runs'):
        sha = args[1].split('/')[-2]
        ci = state.get('checks', {}).get(sha, state.get('ci', 'success'))
        print(json.dumps({'check_runs': ci if isinstance(ci, list) else runs(ci)}))
    elif cmd == 'release create':
        print(f'https://github.com/example/repo/releases/tag/{args[2]}')
    elif cmd == 'release view':
        return 1
    with open(path, 'w') as f:
        json.dump(state, f)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
