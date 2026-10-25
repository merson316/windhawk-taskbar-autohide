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
! git rev-parse -q --verify "refs/tags/v$V" >/dev/null || { echo "tag v$V already exists" >&2; exit 1; }
# Every use of main below goes through BASE, resolved once from the branch
# itself: a tag named main would otherwise win and could hide commits.
git show-ref -q --verify refs/tags/main && { echo "a tag named main exists; refusing" >&2; exit 1; }
git switch -q main
git fetch -q --no-tags origin refs/heads/main
git merge -q --ff-only FETCH_HEAD
BASE=$(git rev-parse --verify -q 'refs/heads/main^{commit}')

if [ -n "$PR" ]; then
  # This runs with the signing key loaded, so the PR must not be able to
  # change what runs: same repo only, src/ and README.md only, and exactly
  # the commit shown here.
  INFO=$(gh pr view "$PR" --json isCrossRepository,headRefOid,baseRefName,body)
  field() { python3 -c "import json,sys; d=json.load(sys.stdin); print($1)" <<<"$INFO"; }
  [ "$(field "d['isCrossRepository']")" = False ] || { echo "PR #$PR is from a fork; refusing" >&2; exit 1; }
  [ "$(field "d['baseRefName']")" = main ] || { echo "PR #$PR does not target main; refusing" >&2; exit 1; }
  SHA=$(field "d['headRefOid']")
  if [ -z "$OSB" ]; then
    OSB=$(field "d['body']" | sed -n 's/.*Tested on OS build (winver): `\([0-9][0-9.]*\)`.*/\1/p')
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

if [ -n "$OSB" ]; then
  .venv/bin/python tools/release_notes.py --version "$V" --record "$OSB"
  git add docs/compatibility.json
  git commit -q -S -m "docs: record $V as tested on $OSB"
fi

git push -q origin refs/heads/main:refs/heads/main
git tag -s "v$V" -m "v$V"
git push -q origin "v$V"

WORK=$(mktemp -d)
.venv/bin/python tools/release_notes.py --version "$V" > "$WORK/notes.md"
"${EDITOR:-nano}" "$WORK/notes.md"
cp "$SRC" "$WORK/taskbar-autohide-any-edge.wh.cpp"
gh release create "v$V" --title "v$V" --notes-file "$WORK/notes.md" "$WORK/taskbar-autohide-any-edge.wh.cpp"
if [ -n "$PR" ]; then
  gh pr close "$PR" --comment "Rebased onto main with signed commits and released in v$V." --delete-branch
fi
rm -rf "$WORK"
