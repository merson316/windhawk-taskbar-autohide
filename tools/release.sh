#!/usr/bin/env bash
# Cut a release, signed with the owner's key, so it cannot run in CI.
#
#   tools/release.sh <version> [--pr N] [--os-build 26300.9807]
#
# With --pr, rebases the approved PR onto main with signed commits (never the
# merge button) and takes the tested OS build from the PR's checklist.
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
git switch -q main
git pull -q --ff-only origin main

if [ -n "$PR" ]; then
  BR=$(gh pr view "$PR" --json headRefName --jq .headRefName)
  if [ -z "$OSB" ]; then
    OSB=$(gh pr view "$PR" --json body --jq .body | sed -n 's/.*Tested on OS build (winver): `\([0-9][0-9.]*\)`.*/\1/p')
  fi
  [ -n "$OSB" ] || { echo "PR #$PR does not record the tested OS build" >&2; exit 1; }
  git fetch -q origin "$BR"
  git switch -q -c "release-$V" "origin/$BR"
  git rebase -q --gpg-sign main
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

git push -q origin main
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
