#!/usr/bin/env bash
# Cut a release, signed with the owner's key, so it cannot run in CI.
#
#   tools/release.sh <version> [--pr N] [--os-build 26300.9807]
#
# With --pr, rebases the approved PR onto main with signed commits (never the
# merge button) and takes the tested OS build from the PR's checklist. The PR
# must come from this repo, touch only src/ and README.md, and is pinned to
# the commit shown for confirmation.
# --os-build records that this version was tested on that build, with the
# taskbar.dll installed on this PC. The notes open in $EDITOR before
# publishing: rewrite the Changes draft in plain language there.
#
# Everything that can fail runs before the first push. If the release itself
# fails after the tag is pushed, re-run the same command to finish it.
set -euo pipefail
cd "$(dirname "$0")/.."

V=${1:?usage: tools/release.sh <version> [--pr N] [--os-build BUILD]}; shift
PR="" OSB=""
while [ $# -gt 0 ]; do
  case $1 in
    --pr) PR=$2; shift 2 ;;
    --os-build) OSB=$2; shift 2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
SRC=src/taskbar-autohide-any-edge.wh.cpp

[ -z "$(git status --porcelain)" ] || { echo "working tree is dirty" >&2; exit 1; }
can_sign() {   # sign a scratch file exactly as git would, through gpg.ssh.program
  local d prog key rc
  prog=$(git config gpg.ssh.program || echo ssh-keygen); prog=${prog/#\~/$HOME}
  key=$(git config user.signingkey) || return 1; key=${key/#\~/$HOME}
  d=$(mktemp -d); echo probe > "$d/p"
  "$prog" -Y sign -n git -f "$key" "$d/p" >/dev/null 2>&1; rc=$?
  rm -rf "$d"; return $rc
}
can_sign || { echo "signing is unavailable: run bin/keys-load.sh, then retry" >&2; exit 1; }
# Every use of main below goes through BASE, resolved once from the branch
# itself: a tag named main would otherwise win and could hide commits.
git show-ref -q --verify refs/tags/main && { echo "a tag named main exists; refusing" >&2; exit 1; }
git switch -q main
git fetch -q --no-tags origin refs/heads/main
git merge -q --ff-only FETCH_HEAD
BASE=$(git rev-parse --verify -q 'refs/heads/main^{commit}')
RESUME=""
if git rev-parse -q --verify "refs/tags/v$V" >/dev/null; then
  # Only an earlier run of this script, stopped after pushing its tag.
  [ "$(git rev-parse "v$V^{commit}")" = "$BASE" ] && git tag -v "v$V" >/dev/null 2>&1 ||
    { echo "tag v$V already exists and is not this release's tag on main" >&2; exit 1; }
  ! gh release view "v$V" >/dev/null 2>&1 || { echo "release v$V already exists" >&2; exit 1; }
  RESUME=1
  echo "resuming v$V: the tag is pushed, publishing the release"
fi

if [ -n "$PR" ] && [ -z "$RESUME" ]; then
  # This runs with the signing key loaded, so the PR must not be able to
  # change what runs: same repo only, src/ and README.md only, and exactly
  # the commit shown here.
  INFO=$(gh pr view "$PR" --json isCrossRepository,isDraft,headRefOid,baseRefName,body)
  field() { python3 -c "import json,sys; d=json.load(sys.stdin); print($1)" <<<"$INFO"; }
  [ "$(field "d['isCrossRepository']")" = False ] || { echo "PR #$PR is from a fork; refusing" >&2; exit 1; }
  [ "$(field "d['baseRefName']")" = main ] || { echo "PR #$PR does not target main; refusing" >&2; exit 1; }
  [ "$(field "d['isDraft']")" = False ] || { echo "PR #$PR is still a draft; mark it ready once tested" >&2; exit 1; }
  if field "d['body']" | grep -E '^[[:space:]]*- \[ \]' >/dev/null; then
    echo "PR #$PR has unticked checklist items; finish testing first" >&2; exit 1
  fi
  SHA=$(field "d['headRefOid']")
  if [ -z "$OSB" ]; then
    OSB=$(field "d['body']" | sed -n 's/.*Tested on OS build (winver): `\([0-9][0-9.]*\)`.*/\1/p' | head -1)
  fi
  [ -n "$OSB" ] || { echo "PR #$PR does not record the tested OS build" >&2; exit 1; }
  git fetch -q --no-tags origin "refs/pull/$PR/head"
  [ "$(git rev-parse FETCH_HEAD)" = "$SHA" ] || { echo "PR #$PR moved while releasing; refusing" >&2; exit 1; }
  # Judge the commits that will be applied, not GitHub's (truncatable) file list.
  [ -z "$(git rev-list --merges "$BASE..$SHA")" ] || { echo "PR #$PR contains a merge commit; refusing" >&2; exit 1; }
  check_paths() {   # stdin: git --raw lines; refuse paths outside src/ and README.md, and non-regular files
    awk -F'\t' '{ split($1, f, " "); mode = (f[2] == "000000") ? substr(f[1], 2) : f[2]; path = $NF
                   if (path !~ /^src\// && path != "README.md") print "outside src/ and README.md: " path
                   else if (mode != "100644" && mode != "100755") print "not a regular file: " path }'
  }
  BAD=$(git log --format= --raw --no-renames "$BASE..$SHA" | check_paths | sort -u)
  [ -z "$BAD" ] || { printf 'PR #%s refused:\n%s\n' "$PR" "$BAD" >&2; exit 1; }
  git show "$SHA:$SRC" | grep -E "^// @version +$V\$" >/dev/null ||
    { echo "PR #$PR's $SRC is not at version $V" >&2; exit 1; }
  git log --stat --format='%h %s' "$BASE..$SHA"
  read -r -p "Rebase and sign exactly these commits (${SHA:0:12})? Type yes: " OK
  [ "$OK" = yes ] || { echo "not confirmed" >&2; exit 1; }
  git switch -q -c "release-$V" "$SHA"
  git rebase -q --gpg-sign "$BASE"
  git merge-base --is-ancestor "$BASE" HEAD || { echo "rebase did not land on main; refusing" >&2; git switch -q main; exit 1; }
  BAD=$(git diff --raw --no-renames "$BASE" HEAD | check_paths)
  [ -z "$BAD" ] || { printf 'rebased PR #%s refused:\n%s\n' "$PR" "$BAD" >&2; git switch -q main; exit 1; }
  git switch -q main
  git merge -q --ff-only "release-$V"
  git branch -q -D "release-$V"
fi

grep -qE "^// @version +$V\$" "$SRC" || { echo "$SRC is not at version $V" >&2; exit 1; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
if [ -z "$RESUME" ]; then
  if [ -n "$OSB" ]; then
    .venv/bin/python tools/release_notes.py --version "$V" --record "$OSB"
    git add docs/compatibility.json
    git diff --cached --quiet || git commit -q -S -m "docs: record $V as tested on $OSB"
  fi
  .venv/bin/python tools/check-builds.py >/dev/null
  git add docs/builds.json
  git diff --cached --quiet || git commit -q -S -m "docs: refresh the taskbar builds the mod should work on"
fi
.venv/bin/python tools/release_notes.py --version "$V" > "$WORK/notes.md"
"${EDITOR:-nano}" "$WORK/notes.md"
cp "$SRC" "$WORK/taskbar-autohide-any-edge.wh.cpp"

if [ -z "$RESUME" ]; then
  git push -q origin refs/heads/main:refs/heads/main
  git tag -s "v$V" -m "v$V"
  git push -q origin "v$V"
fi
gh release create "v$V" --title "v$V" --notes-file "$WORK/notes.md" "$WORK/taskbar-autohide-any-edge.wh.cpp"
if [ -n "$PR" ]; then
  gh pr close "$PR" --comment "Rebased onto main with signed commits and released in v$V." --delete-branch
fi
