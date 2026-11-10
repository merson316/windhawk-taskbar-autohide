# Hands-off release for new taskbar builds

## Intent

A Windows update that brings a new taskbar.dll should need nothing from the
owner. Today the watcher matches the new build and opens a draft PR. The
owner then ticks a hand-test checklist, runs `tools/release.sh --pr N`, edits
the notes and pastes the new source into Windhawk. After this change, the
watcher takes the PR all the way through: CI green, signed merge, GitHub
release, and the new version installed in the owner's Windhawk. The owner
only sees a toast.

The GitHub release still matters: one friend also uses the mod and installs
releases by hand from GitHub. Nothing installs on the friend's PC
automatically.

**Why it is safe to drop the hand test:** this is the third build update the
owner has been through, and the matcher's evidence has been right each time.
The mod also checks the bytes at every address before it hooks, and it stands
down with a warning rather than crash the taskbar (`ScanForOffsets`,
`g_fatalReason`). Only two people use the mod, so a bad release costs little
and shows up quickly.

## Goals

1. Once CI passes on a PR the watcher opened, the watcher releases it with no
   prompt, editor or checklist.
2. Every commit and tag is still signed with the owner's key on the owner's
   PC. CI never holds a key, and the merge button is never used.
3. The watcher installs the released version in the owner's Windhawk, before
   the restart if the update is still staged, so the taskbar works from the
   first boot on the new build.
4. The release notes say plainly that the build was matched automatically and
   not tested by hand.
5. Any failure stops cleanly and toasts the owner. What the watcher did up to
   that point stays done, and the next run, or the manual flow, picks it up.

## Non-goals

- Updating the friend's PC.
- Releasing anything other than the watcher's own new-build PRs. Releases
  with code changes keep the manual `tools/release.sh` flow.
- Checking that auto-hide actually works after the restart. Windhawk logs
  through debug output, not a file, so the watcher cannot read it.
- Adding the mod to Windhawk's catalogue.

## Anti-goals (unacceptable even if the flow works)

- A key or token in CI that can push to main or sign.
- Auto-releasing a PR that someone other than the owner's watcher could have
  changed: a fork, a branch with a commit the owner's key did not sign, or a
  change to anything other than `src/` and `README.md`.
- Touching the owner's checkout or uncommitted edits. The watcher already
  works in a temporary worktree, and that stays.
- Overwriting the installed mod's settings, or leaving Windhawk half-updated:
  a new DLL whose registry entry still points at the old one, or the reverse.
- A run that re-toasts on every sign-in about the same lasting problem. Use
  the existing `notify_once`.

## How it works (decided)

### 1. Release (in the watcher, on the owner's PC)

- **When:** at the end of every watcher run, for each open PR from a
  `build/*` branch on this repo. That covers a PR opened on this run and any
  PR still pending from an earlier one, including the existing PR for
  26100.9974.
- **Stop signal:** the watcher now opens its PRs as ready, not as drafts. A
  draft PR is never auto-released, so converting a PR to draft, or closing
  it, stops the release. The existing draft PR for 26100.9974 releases
  itself once the owner marks it ready. *(My call.)*
- **CI:** release only when both required checks ("Python tests" and
  "Compile with Windhawk's compiler") have *succeeded* on the PR's exact head
  commit. Admins bypass the ruleset, so nothing on GitHub enforces this, and
  the watcher must. Wait up to about 15 minutes on a PR opened in this run.
  For an older PR, check once without waiting. Pending or missing: leave it
  for the next run. Failed: toast once and leave the PR for the owner.
  *(My call.)*
- **One at a time:** release pending PRs oldest first, one per run step.
  Every watcher PR bumps to the same next version from the same main. So
  once one releases, any other open `build/*` PR is stale: the watcher
  closes it with a comment and deletes its branch. Because the branch is
  gone, the same run, or the next one, matches that build again from the new
  main and opens a fresh PR. *(My call.)*
- **Trust check before release, in addition to all of `release.sh`'s existing
  `--pr` checks** (same repo, targets main, no merge commits, only regular
  files under `src/` and `README.md`, head pinned to the exact commit):
  - every commit in `main..head` has a valid signature from *this PC's*
    signing key (`user.signingkey`, the key the watcher commits with). The
    shared allowed-signers file also lists the owner's other devices, so it
    is not enough on its own. Check against a signers list holding only
    this key. *(My call: watcher PRs are only ever made on this PC.)*
  - the change adds exactly one `kKnownBuilds` entry for the PR's build,
    bumps `@version`, and adds one README row.
- **Release:** the `release.sh --pr` logic runs without a prompt: no "type
  yes", no `$EDITOR`. The draft and checklist gates are skipped only on this
  path. The version is read from the PR's `@version`. The notes come from
  `release_notes.py` unedited, plus a line saying the build was matched
  automatically and not tested by hand. The script rebases with signed
  commits, fast-forwards main, pushes, adds a signed tag and creates the
  release with the `.wh.cpp` attached. Then it closes the PR with a comment
  and deletes the branch. This works with the current rulesets: admins
  bypass the required checks, and signatures and fast-forward-only still
  apply.
- **Resume:** `release.sh` already resumes after a pushed tag, and the auto
  path keeps that.

### 2. Install in the owner's Windhawk

Windhawk keeps a local mod in three places:

- the source in `%ProgramData%\Windhawk\ModsSource\local@taskbar-autohide-any-edge.wh.cpp`;
- a compiled DLL in `%ProgramData%\Windhawk\Engine\Mods\64\local@taskbar-autohide-any-edge_<version>_<random>.dll`;
- `HKLM\SOFTWARE\Windhawk\Engine\Mods\local@taskbar-autohide-any-edge`,
  whose `Version` and `LibraryFileName` values say which DLL to load. The
  key also holds `Settings`, `Disabled`, `LoggingEnabled`, `Include` and the
  other values, which must be kept.

After the release is published, the watcher:

1. compiles the released source with `tools/compile-mod.sh`, which uses
   Windhawk's own compiler, to a DLL with a new `<version>_<random>` name;
2. checks that the layout looks as described above: the paths exist, the
   registry key exists with those values, and `windhawk.ini` reports
   `Portable=0` and the expected `AppDataPath`/`RegistryKey`. If anything
   looks different, it does not guess: it toasts "released, but install it in
   Windhawk by hand" and stops;
3. copies the DLL into place, writes the new source, and then updates
   `Version` and `LibraryFileName` in one step, last of all, so Windhawk
   never sees a half-installed mod;
4. leaves the old DLL in place, as Windhawk itself does. Windhawk may clean
   it up later.

The HKLM write needs elevation, and the watcher task runs unelevated. So
`install-watch-task.ps1`, run once as admin, also registers a second task
that runs with highest privileges and does only the copy-and-register part
of the install. The watcher compiles unelevated, then starts that task.

**Threat model (the owner's decision): mistakes only, not malware.** The
owner's account is an admin, and UAC is not a security boundary for an
admin account, so the elevated task may run the repo's own script. It
should still refuse obvious mistakes: a DLL whose name does not match the
version being installed, a version not newer than the one installed, or
a missing or empty file.

Toast on success: "v1.19.0 released and installed for taskbar 26100.9974".

### 3. What else changes

- The PR template for watcher PRs says the PR will release itself once CI
  passes, and that the owner can close it or convert it to draft to stop
  that.
- `docs/compatibility.json` does not record an OS build for auto releases,
  because it means "tested on". *(My call: recording the owner's winver
  there would claim a test that did not happen.)*

## Left to the builder

- Where the code lives: the watcher, a shared module that `release.sh` and the
  watcher both call, or a non-interactive mode of `release.sh`. Keep one copy
  of the safety checks, not two.
- How the watcher waits for CI (`gh pr checks --watch`, polling the checks
  API, ...), and the exact timeout.
- How "exactly one entry" is checked: regenerating the expected change from
  the DLL and comparing, or parsing the diff.
- How the elevated task is started and handed the version, and how its result
  gets back to the watcher.
- Concurrency between two watcher runs. Check whether one exists today and
  add a lock if needed.
- Tests: follow `tests/test_watch.py`'s throwaway-origin pattern for the
  release path. The install step should be testable against a fake
  `ProgramData` tree and a fake registry layer.

## Open question to verify first (spike)

**Does Windhawk pick up a changed `LibraryFileName`/`Version` without its UI?**
Windhawk's engine is believed to watch the mods registry key and reload a mod
when it changes, which is how the UI's Compile button takes effect. Prove
this on the owner's PC before building step 2. If it does not reload, the
fallback is `windhawk.exe -restart -tray-only` after the install.
