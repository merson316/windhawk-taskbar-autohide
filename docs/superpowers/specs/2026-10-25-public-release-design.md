# Going public: history cleanup, build automation and releases — design

Date: 2026-10-25
Status: draft, reviewed, awaiting approval

## Intent

The repo is private today. The owner will make it public once three things are
in place, so that other people whose auto-hide broke after a Windows update can
find, trust and install the mod:

1. The full git history is free of personal information.
2. New `taskbar.dll` builds that ship without published symbols are supported
   with as little manual work as possible.
3. Each mod version is a GitHub release that says which Windows builds it works
   with.

Users install by downloading `src/taskbar-autohide-any-edge.wh.cpp` and pasting
it into Windhawk's editor. The mod is not in Windhawk's catalogue.

## Background

- The mod hooks seven functions in `taskbar.dll`, found through Microsoft's debug
  symbols (PDB), which Windhawk downloads.
- Microsoft sometimes publishes a build's PDB late. For 10.0.26100.9807 it was
  still missing after eight days.
- In that case Microsoft's symbol server does not have the DLL either, and
  Winbindex does not list the build. Only a PC that has installed the update
  has the file.
- Version 1.17.0 added `kKnownBuilds` to the mod: per-build addresses, keyed by
  PDB GUID and age, used only when symbols fail. The 9807 entry was matched by
  hand with Ghidra against 9549, and tested end to end.
- A cloud routine, "Taskbar 9807 symbol watch" (daily 08:00 UTC), checks whether the 9807 PDB has been published. When it
  has, the routine opens a GitHub issue comparing the RVAs. The Default cloud
  environment allows `msdl.microsoft.com` and `*.blob.core.windows.net`.

## Part 1: history cleanup (do first)

Decided by the owner:
- Clean the full history, not only the current files.
- Force-pushing `main` and the tags is allowed for this repo. This is an
  explicit exception to the owner's global no-force-push rule, for this
  cleanup only.
- Every rewritten commit and tag must end up signed. Never disable signing.

The scan found the following (2026-10-25, all 200 commits and 26 tags):
- **Remove:** 58 `Claude-Session: https://claude.ai/code/session_…` lines in
  commit messages, from two sessions. Delete those lines and keep the rest of
  each message.
- **Generalise**, in every commit that touches them (mostly
  `docs/windhawk-config.md` and `docs/findings-phase1.md`):
  - Drive-letter game-library paths become placeholders like
    `<GameDrive>:\SteamLibrary\*`.
  - The list of the owner's own mod targets, including a GPU-vendor process,
    becomes generic examples.
- **Must not be in history (owner's decision):** `research/taskbar-pdb-strings.txt`
  and `research/taskbarview-pdb-strings.txt`, raw `strings` dumps of Microsoft
  PDBs. On checking, they are git-ignored and were never committed. The
  rewrite verifies that no blob in history matches them. `research/symbol-map.tsv` stays: it is a short list of the hooked
  symbol names.
- **Keep:** monitor sizes (3440×1440, 2560×1440) and Discord as the test app.
  Both explain real findings and identify little.
- **Already clean:** author, committer and tagger are all
  `merson316@users.noreply.github.com`. There are no real emails, Windows
  username, machine name, user paths, IP addresses, other devices, tokens or
  keys.

Requirements:
- Today 196 of 200 commits are unsigned. After the rewrite, all of them are
  signed with the owner's existing SSH signing key.
- Preserve author and committer dates.
- The 26 existing tags are recreated on the rewritten commits as signed
  annotated tags, keeping their messages. Also add `v1.16.0` and `v1.17.0`.
- Before any force-push, re-run the scan on the rewritten history and show the
  result. It must have zero session URLs and none of the listed setup details.
- Keep a backup of the original history (a bundle) outside the repo until the
  owner says to delete it.
- **Recreate the GitHub repo (owner's decision).** A force-push leaves the old
  commits reachable by their IDs on GitHub. The repo has no issues, PRs,
  releases or forks, so:
  1. Delete the GitHub repo.
  2. Recreate it with the same name, private.
  3. Push only the cleaned history and the tags.

  Ask the owner to confirm immediately before the delete. Afterwards the owner
  may need to re-grant the Claude GitHub app access, and the cloud routine must
  be re-run once to confirm it still has access.
- `dev/`, `research/` (minus the dumps) and `docs/superpowers/` stay public.
  They are useful to anyone extending the mod. (Builder's call.)

## Part 2: automation for builds without symbols

Approved shape:

**Matcher (`tools/match-build.py`)** runs on the owner's PC (WSL). Inputs:
- the new `taskbar.dll`
- a reference build that has a PDB: the newest such build, taken from WinSxS
  plus the Windhawk symbol cache, or downloaded from msdl by
  timestamp+size for the DLL and GUID+age for the PDB

It finds the seven functions with deterministic anchors, the same ones that
worked by hand for 9807:

| Function | Anchor |
|---|---|
| `SetAutoHideFlags` | only referencer of the string `IsAutoHideEnabled` |
| `Unhide` | referencer of `AutoHideOnTaskbarShow`; there are two, pick the one most similar to the reference (the other is in `secondarytray.cpp`) |
| `GetStuckPlace` | exact byte pattern |
| `GetAutoHideFlags` | the mod's existing shape scan |
| `GetDockedRect` | a caller of `GetAutoHideFlags` that matches the reference |
| `SlideWindow` | the show/hide duration loads at the offsets the mod checks |
| `PermitAutoHide` | the `[rdi+0x190]` / `[rdi+0x198]` / `[rdi+0x240]` field pattern |

It then confirms each match by normalised-instruction similarity against the
reference, plus identical imported calls and member offsets.

It fails closed. If an anchor is missing or ambiguous, or similarity is below
threshold, it produces no entry and says why, so a person can do a manual
Ghidra pass. Thresholds are calibrated on known pairs: it must reproduce today's
9807 entry, and must reproduce 9549 from 9278, both of which have PDBs.

Output: a `kKnownBuilds` entry and an evidence report (one row per function:
anchor used, similarity, size).

**Trigger** is a Windows scheduled task at sign-in. When `taskbar.dll`'s PDB
identity is new, and neither published on msdl nor already in `kKnownBuilds`,
it runs the matcher. It then:
- opens a draft PR containing only the table entry, README row, the `@version`
  bump and the evidence; never the DLL. Bumping the version in the PR means the
  version the owner tests is the version that ships (builder's call).
- puts a checklist in the PR body, where the owner records the `winver` OS
  build they tested on. The release notes take "Tested on" from it.
- shows a Windows notification

It does nothing on builds that are already handled.

**Human gate (owner's requirement in effect):** the owner compiles the PR's
version in Windhawk, confirms auto-hide works, and merges. Nothing merges
automatically. A wrong address crashes Explorer for everyone on that build.

**Cloud verification** replaces the 9807-only routine with one that reads every
`kKnownBuilds` entry from `main`. For each entry whose PDB has now been
published, it downloads the PDB, compares all seven RVAs and opens one issue
per build. It skips builds that already have an issue.

## Part 3: releases

Decided by the owner:
- Each release carries a note saying which Windows builds it works with.
- Users paste the `.wh.cpp` into Windhawk.
- The audience is other people once the repo is public.

Agreed shape:
- **Compatibility note at the top of each release:**
  - **Tested on:** OS builds as `winver` shows them (for example 26300.9807),
    each with its `taskbar.dll` version.
  - **Built-in support:** builds served by `kKnownBuilds` instead of symbols.
  - **Everything else:** expected to work wherever Windhawk can get symbols,
    unless an update changes the code the mod checks. In that case the mod warns
    rather than crashes.
- **Patch notes:** short, in plain language and written for users, based on the
  commits since the previous release. Not a commit log.
- **Asset:** the `.wh.cpp` attached to each release, so people get that exact
  version.
- **Backfill:** write releases only for versions someone might still want, not
  all 25 tags.
- **Releasing is a local script (`tools/release.sh`), not CI (builder's
  call).** Tags must be signed with the owner's SSH key, which stays in
  1Password, so GitHub Actions cannot make them. The script:
  1. merges the approved PR into `main` locally, with signed commits
  2. pushes `main`
  3. creates the signed tag
  4. drafts the plain-language notes and compatibility section
  5. creates the GitHub release with the `.wh.cpp` attached

  PRs are never merged with GitHub's merge button, because the resulting commit
  would not carry the owner's signature.
- **"Tested on" for backfilled releases (builder's call):**
  - Work it out by matching each tag's date against the dates each
    `taskbar.dll` build was installed on this PC (from WinSxS: 9278 on 27 Aug,
    9549 on 23 Sep, 9807 on 17 Oct) and the OS build at the time.
  - Show the owner the derived table to confirm before publishing.
  - Where it can't be worked out, leave "Tested on" out rather than guess.
- **New builds:** a merged matcher PR leads to a release whose built-in-support
  list gains the new build.

## Goals

- A stranger can tell from the releases page whether a version works on their
  PC, and download it.
- A Windows update without symbols costs the owner one test-and-merge, not a
  reverse-engineering session.

## Non-goals

- Publishing to Windhawk's mod catalogue.
- Supporting builds older than 26200.
- Redistributing Microsoft binaries or PDBs in any form.

## Anti-goals (failure even if it works)

- Any personal information or Claude session link in the public history.
- An address table entry that was never tested on a real machine.
- Unsigned commits or tags.
- A matcher that guesses instead of stopping.

## Left to the builder

- The rewrite tooling (`git filter-repo` or similar, then re-signing), and how
  to verify it.
- The similarity metric and thresholds, within the calibration rule above.
- The disassembler library for the matcher (for example capstone or objdump).
- The scheduled task's implementation and how it reaches WSL and `gh`.
- How release notes are drafted, and which older versions get backfilled.
- Where the compatibility data lives in the repo.
