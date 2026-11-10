#!/usr/bin/env bash
# Cut a release, signed with the owner's key, so it cannot run in CI.
#
#   tools/release.sh <version> [--pr N] [--os-build 26300.9807]
#   tools/release.sh <version> --pr N --auto --sha SHA
#
# With --pr, rebases the approved PR onto main with signed commits (never the
# merge button) and takes the tested OS build from the PR's checklist. The PR
# must come from this repo, touch only src/ and README.md, and is pinned to
# the commit shown for confirmation.
# --os-build records that this version was tested on that build, with the
# taskbar.dll installed on this PC. The notes open in $EDITOR before
# publishing: rewrite the Changes draft in plain language there.
#
# --auto is for the build watcher: no prompts and no editor. The PR must be
# exactly SHA, from a build/ branch, ready (not a draft), and pass
# tools/check_build_pr.py (signed by this PC's key, one kKnownBuilds entry,
# nothing else). The caller has
# already seen CI pass on SHA. It skips the checklist and the tested-OS-build
# requirement, writes no docs/compatibility.json entry, and says in the notes
# that the build was matched automatically and not tested by hand.
#
# Exit codes: 0 released (or already released), 1 refused or failed, 2
# usage. Under --auto a refusal exits 3 instead, a PR that is a draft exits 4
# (the owner's stop signal, not a problem), and any other non-zero exit is a
# failure that a later run may get past (network, signing key not loaded yet).
#
# Everything that can fail runs before the first push. If the release itself
# fails after the tag is pushed, re-run the same command to finish it; once
# the release exists, a re-run prints its URL and exits 0. Commenting on and
# closing the PR and deleting its branch come after the release and are best
# effort: a failure there is a warning, not a failed release. (Pushing main
# usually makes GitHub mark the PR merged already: its head is now on main.)
set -euo pipefail
cd "$(dirname "$0")/.."

V=${1:?usage: tools/release.sh <version> [--pr N] [--os-build BUILD]}; shift
PR="" OSB="" AUTO="" PINNED=""
while [ $# -gt 0 ]; do
  case $1 in
    --pr) PR=$2; shift 2 ;;
    --os-build) OSB=$2; shift 2 ;;
    --auto) AUTO=1; shift ;;
    --sha) PINNED=$2; shift 2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
if [ -n "$AUTO" ]; then
  { [ -n "$PR" ] && [ -n "$PINNED" ] && [ -z "$OSB" ]; } ||
    { echo "usage: tools/release.sh <version> --pr N --auto --sha SHA (no --os-build)" >&2; exit 2; }
elif [ -n "$PINNED" ]; then
  echo "--sha is only for --auto" >&2; exit 2
fi
SRC=src/taskbar-autohide-any-edge.wh.cpp
# Exit code of a refusal: under --auto, 3 tells the watcher "this PR will not
# release as it is" apart from a failure (1, or whatever a failing command
# returns under set -e) that a later run may get past, such as a network
# error or the signing key not being loaded yet.
R=1 D=1; [ -z "$AUTO" ] || R=3 D=4
if [ -n "$AUTO" ]; then CLOSE="Released automatically in v$V once CI passed."
else CLOSE="Rebased onto main with signed commits and released in v$V."; fi
jget() { python3 -c "import json,sys; v=json.load(sys.stdin).get('$1'); print('' if v is None else v)"; }
warn() { echo "warning: $*" >&2; }
finish() {   # $1 = new: the release was just made. Best effort: it exists either way.
  [ -n "$PR" ] || return 0
  local view state branch cross
  view=$(gh pr view "$PR" --json state,headRefName,isCrossRepository) ||
    { warn "cannot read PR #$PR; close it and delete its branch by hand"; return 0; }
  state=$(jget state <<<"$view"); branch=$(jget headRefName <<<"$view"); cross=$(jget isCrossRepository <<<"$view")
  if [ "${1:-}" = new ] || [ "$state" = OPEN ]; then
    gh pr comment "$PR" --body "$CLOSE" >/dev/null || warn "could not comment on PR #$PR"
  fi
  if [ "$state" = OPEN ]; then
    gh pr close "$PR" >/dev/null || warn "could not close PR #$PR"
  fi
  # Only this repo's own branch, never main, and under --auto only a build/ one.
  [ "$cross" = False ] && [ -n "$branch" ] && [ "$branch" != main ] || return 0
  [ -z "$AUTO" ] || [[ $branch == build/* ]] || return 0
  if git ls-remote -q --exit-code --heads origin "refs/heads/$branch" >/dev/null 2>&1; then
    git push -q origin --delete "refs/heads/$branch" || warn "could not delete branch $branch"
  fi
}

[ -z "$(git status --porcelain)" ] || { echo "working tree is dirty" >&2; exit "$R"; }
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
git show-ref -q --verify refs/tags/main && { echo "a tag named main exists; refusing" >&2; exit "$R"; }
git switch -q main
git fetch -q --no-tags origin refs/heads/main
git merge -q --ff-only FETCH_HEAD
BASE=$(git rev-parse --verify -q 'refs/heads/main^{commit}')
RESUME=""
if git rev-parse -q --verify "refs/tags/v$V" >/dev/null; then
  # Only an earlier run of this script, stopped after pushing its tag.
  [ "$(git rev-parse "v$V^{commit}")" = "$BASE" ] && git tag -v "v$V" >/dev/null 2>&1 ||
    { echo "tag v$V already exists and is not this release's tag on main" >&2; exit "$R"; }
  if [ -n "$AUTO" ]; then
    # Every build PR bumps to the same version, so v$V on main may be another
    # PR's release: resume only if it holds this PR's change.
    git fetch -q --no-tags origin "refs/pull/$PR/head"
    [ "$(git rev-parse FETCH_HEAD)" = "$PINNED" ] || { echo "PR #$PR moved: head is not ${PINNED:0:12}" >&2; exit "$R"; }
    git merge-base --is-ancestor "$PINNED" "$BASE" ||
      [ "$(git rev-parse "$PINNED:$SRC")" = "$(git rev-parse "v$V:$SRC")" ] ||
      { echo "v$V on main is another PR's release, not PR #$PR's; not resuming" >&2; exit 1; }
  fi
  if VIEW=$(gh release view "v$V" --json url 2>/dev/null); then
    echo "v$V is already released"
    jget url <<<"$VIEW"
    finish || true
    exit 0
  fi
  RESUME=1
  echo "resuming v$V: the tag is pushed, publishing the release"
fi

if [ -n "$PR" ] && [ -z "$RESUME" ]; then
  # This runs with the signing key loaded, so the PR must not be able to
  # change what runs: same repo only, src/ and README.md only, and exactly
  # the commit shown here.
  INFO=$(gh pr view "$PR" --json isCrossRepository,isDraft,headRefOid,headRefName,baseRefName,body)
  field() { python3 -c "import json,sys; d=json.load(sys.stdin); print($1)" <<<"$INFO"; }
  [ "$(field "d['isCrossRepository']")" = False ] || { echo "PR #$PR is from a fork; refusing" >&2; exit "$R"; }
  if [ -n "$AUTO" ] && [[ "$(field "d['headRefName']")" != build/* ]]; then
    echo "PR #$PR is not from a build/ branch; refusing" >&2; exit "$R"
  fi
  [ "$(field "d['baseRefName']")" = main ] || { echo "PR #$PR does not target main; refusing" >&2; exit "$R"; }
  [ "$(field "d['isDraft']")" = False ] || { echo "PR #$PR is still a draft; mark it ready once tested" >&2; exit "$D"; }
  if [ -z "$AUTO" ] && field "d['body']" | grep -E '^[[:space:]]*- \[ \]' >/dev/null; then
    echo "PR #$PR has unticked checklist items; finish testing first" >&2; exit "$R"
  fi
  SHA=$(field "d['headRefOid']")
  [ -z "$AUTO" ] || [ "$SHA" = "$PINNED" ] || { echo "PR #$PR moved: head is ${SHA:0:12}, expected ${PINNED:0:12}" >&2; exit "$R"; }
  if [ -z "$OSB" ] && [ -z "$AUTO" ]; then
    OSB=$(field "d['body']" | sed -n 's/.*Tested on OS build (winver): `\([0-9][0-9.]*\)`.*/\1/p' | head -1)
  fi
  [ -n "$OSB" ] || [ -n "$AUTO" ] || { echo "PR #$PR does not record the tested OS build" >&2; exit "$R"; }
  git fetch -q --no-tags origin "refs/pull/$PR/head"
  [ "$(git rev-parse FETCH_HEAD)" = "$SHA" ] || { echo "PR #$PR moved while releasing; refusing" >&2; exit "$R"; }
  # Judge the commits that will be applied, not GitHub's (truncatable) file list.
  [ -z "$(git rev-list --merges "$BASE..$SHA")" ] || { echo "PR #$PR contains a merge commit; refusing" >&2; exit "$R"; }
  check_paths() {   # stdin: git --raw lines; refuse paths outside src/ and README.md, and non-regular files
    awk -F'\t' '{ split($1, f, " "); mode = (f[2] == "000000") ? substr(f[1], 2) : f[2]; path = $NF
                   if (path !~ /^src\// && path != "README.md") print "outside src/ and README.md: " path
                   else if (mode != "100644" && mode != "100755") print "not a regular file: " path }'
  }
  BAD=$(git log --format= --raw --no-renames "$BASE..$SHA" | check_paths | sort -u)
  [ -z "$BAD" ] || { printf 'PR #%s refused:\n%s\n' "$PR" "$BAD" >&2; exit "$R"; }
  git show "$SHA:$SRC" | grep -E "^// @version +$V\$" >/dev/null ||
    { echo "PR #$PR's $SRC is not at version $V" >&2; exit "$R"; }
  if [ -n "$AUTO" ]; then
    TRUST=$(.venv/bin/python tools/check_build_pr.py "$BASE" "$SHA" 2>&1) ||
      { printf 'PR #%s refused:\n%s\n' "$PR" "$TRUST" >&2; exit "$R"; }
  fi
  git log --stat --format='%h %s' "$BASE..$SHA"
  if [ -z "$AUTO" ]; then
    read -r -p "Rebase and sign exactly these commits (${SHA:0:12})? Type yes: " OK
    [ "$OK" = yes ] || { echo "not confirmed" >&2; exit "$R"; }
  fi
  git switch -q -c "release-$V" "$SHA"
  git rebase -q --gpg-sign "$BASE"
  git merge-base --is-ancestor "$BASE" HEAD || { echo "rebase did not land on main; refusing" >&2; git switch -q main; exit "$R"; }
  BAD=$(git diff --raw --no-renames "$BASE" HEAD | check_paths)
  [ -z "$BAD" ] || { printf 'rebased PR #%s refused:\n%s\n' "$PR" "$BAD" >&2; git switch -q main; exit "$R"; }
  git switch -q main
  git merge -q --ff-only "release-$V"
  git branch -q -D "release-$V"
fi

grep -qE "^// @version +$V\$" "$SRC" || { echo "$SRC is not at version $V" >&2; exit "$R"; }

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
NOTES_ARGS=()
if [ -n "$AUTO" ]; then
  # Build and reference come from the watcher's commit message; failing that,
  # from the README row the PR adds. With neither, refuse: the notes would lie.
  git fetch -q --no-tags origin "refs/pull/$PR/head"
  [ "$(git rev-parse FETCH_HEAD)" = "$PINNED" ] || { echo "PR #$PR moved while releasing; refusing" >&2; exit "$R"; }
  MSG=$(git log -1 --format=%B "$PINNED")
  NEWBUILD=$(sed -n '1s/^feat: built-in addresses for taskbar\.dll \([0-9][0-9.]*\)$/\1/p' <<<"$MSG")
  REF=$(sed -n 's/^Matched against \([0-9][0-9.]*\) by tools\/match-build\.py\.$/\1/p' <<<"$MSG" | head -1)
  if [ -z "$NEWBUILD" ] || [ -z "$REF" ]; then
    ROW=$(git log -p --format= --no-renames "$BASE..$PINNED" -- README.md |
          sed -n 's/^+| \([0-9][0-9.]*\) | \([0-9][0-9.]*\) |$/\1 \2/p')
    [ "$(wc -l <<<"$ROW")" = 1 ] && [ -n "$ROW" ] ||
      { echo "cannot tell which build this PR adds; refusing" >&2; exit "$R"; }
    NEWBUILD=${ROW% *}; REF=${ROW#* }
  fi
  echo "Built-in addresses for taskbar.dll $NEWBUILD, matched automatically against $REF. Not tested by hand." > "$WORK/changes.txt"
  NOTES_ARGS=(--changes "$WORK/changes.txt")
fi
.venv/bin/python tools/release_notes.py --version "$V" ${NOTES_ARGS[@]+"${NOTES_ARGS[@]}"} > "$WORK/notes.md"
[ -n "$AUTO" ] || "${EDITOR:-nano}" "$WORK/notes.md"
cp "$SRC" "$WORK/taskbar-autohide-any-edge.wh.cpp"

if [ -z "$RESUME" ]; then
  git push -q origin refs/heads/main:refs/heads/main
  git tag -s "v$V" -m "v$V"
  git push -q origin "v$V"
fi
gh release create "v$V" --title "v$V" --notes-file "$WORK/notes.md" "$WORK/taskbar-autohide-any-edge.wh.cpp"
finish new || true
