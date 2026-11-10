# Hands-off Build Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Once CI passes on a PR the watcher opened, the watcher releases it with signed commits and installs the new version into the owner's Windhawk, with no input from the owner.

**Architecture:**
- The watcher (`tools/watch_build.py`) still opens one PR per new taskbar build, now as a ready PR, not a draft.
- A new step at the end of each run (`tools/autorelease.py`) finds open `build/*` PRs whose CI passed. It runs `tools/release.sh --auto` on them, oldest first, inside a throwaway clone.
- `release.sh --auto` keeps every existing `--pr` check and adds a trust check (`tools/check_build_pr.py`): this PC's signature, and exactly one new build entry.
- After a release, `tools/windhawk_install.py` compiles the tagged source unelevated and puts the result in a staging folder. A second scheduled task, which runs with highest privileges, then calls `tools/install-mod.ps1` to copy it into Windhawk and update the registry.

**Tech Stack:** Python 3.13 (tools/, pytest), bash (`release.sh`), PowerShell 5.1 (scheduled tasks, elevated install), `gh` CLI, git with SSH signing.

**Spec:** `docs/superpowers/specs/2026-11-10-hands-off-build-release-design.md`

## Global Constraints

- Every commit and tag is signed with the owner's key on the owner's PC. Never `--no-gpg-sign` and never `-c commit.gpgsign=false`. If signing fails, stop and toast. CI never holds a key or a token that can push.
- Never force-push, never use GitHub's merge button, never push to a remote named `upstream`.
- Auto-release only same-repo PRs from a `build/*` branch, not drafts, whose commits change only `src/` and `README.md`, with both required checks ("Python tests", "Compile with Windhawk's compiler") *succeeded* on the exact head SHA.
- Signatures are verified against this PC's key only (`git config user.signingkey`), not against `~/claude-config/ssh/allowed_signers`.
- The owner's checkout and its uncommitted edits are never touched. All git work happens in temporary worktrees or clones.
- Release notes for auto releases include the line: `Built-in addresses for taskbar.dll {build}, matched automatically against {reference}. Not tested by hand.`
- Auto releases do not write `docs/compatibility.json` (no `--record`).
- Installing never overwrites the mod's `Settings` subkey, `Disabled`, `LoggingEnabled`, `Include`, `Exclude` or `Architecture`. Only `Version` and `LibraryFileName` change, with `LibraryFileName` written last.
- The install threat model is "mistakes only": the elevated task may run the repo's script.
- Lasting problems toast once (`notify_once`), not on every run.
- Never put a Claude session URL in commits, PRs or comments.

## Review Focus

1. **The owner's checkout is dirty or on another branch when the watcher releases.** Expected: the release runs in a throwaway clone, and the checkout's branch, HEAD and edits are unchanged. Test: `test_autorelease_leaves_checkout_alone` (Task 5).
2. **The tag is pushed, but `gh release create` fails (network).** Expected: the next run resumes, using `release.sh`'s existing resume path, and does not cut a second version. Test: `test_auto_resumes_after_tag` (Task 4).
3. **The owner has the mod disabled in Windhawk.** Expected: the install updates the version and leaves it disabled. Test: `Install keeps Disabled and Settings` (Task 6).
4. **GitHub is unreachable or slow while waiting for CI.** Expected: the PR is left for the next run, there's no error toast, and the run exits 0. Test: `test_ci_unreachable_leaves_pr` (Task 5).
5. **Two build PRs are open, for a running build and a staged one.** Expected: one releases, the other is closed as stale and reopened from the new main in the same run. Test: `test_second_pr_reopened_after_release` (Task 5).
6. **Two watcher runs at once.** The scheduled task already sets `MultipleInstances IgnoreNew`. A manual `tools/watch-build.sh` alongside one is the remaining case: `release.sh` refuses an existing tag, and its resume logic covers a half-finished one. No lock. *(No test: covered by Task 4's resume test.)*

---

### Task 1: Spike — does Windhawk reload a mod when its registry entry changes?

Throwaway: no code is kept. It decides how Task 6 makes Windhawk load the new DLL.

**Files:** none committed. Write results into the spec's "Open question" section in Task 7.

- [ ] **Step 1: Record what is loaded now**

Run (WSL): `tasklist.exe /m 'local@taskbar-autohide-any-edge*' /fi 'imagename eq explorer.exe'`
Expected: explorer.exe has `local@taskbar-autohide-any-edge_1.18.0_500841.dll` loaded.

- [ ] **Step 2: Point the registry at a copy of the same DLL (needs one UAC prompt from the owner)**

In an elevated PowerShell:
```powershell
$m = "$env:ProgramData\Windhawk\Engine\Mods\64"
Copy-Item "$m\local@taskbar-autohide-any-edge_1.18.0_500841.dll" "$m\local@taskbar-autohide-any-edge_1.18.0_999999.dll"
Set-ItemProperty 'HKLM:\SOFTWARE\Windhawk\Engine\Mods\local@taskbar-autohide-any-edge' LibraryFileName 'local@taskbar-autohide-any-edge_1.18.0_999999.dll'
```

- [ ] **Step 3: Check whether explorer picked it up within 10 s**

Run Step 1's command again.
Expected, if it reloads: `_999999.dll` is loaded and `_500841.dll` is not. If not: run `"/mnt/c/Program Files/Windhawk/windhawk.exe" -restart -tray-only`, then check again.

- [ ] **Step 4: Restore**

Set `LibraryFileName` back to `_500841.dll`, delete `_999999.dll`, and check that Step 1 shows the original again.

- [ ] **Step 5: Record the result** (`RELOAD = registry` or `RELOAD = restart`) for Task 6 to use.

---

### Task 2: Watcher opens ready PRs; template says they release themselves

**Files:**
- Modify: `tools/watch_build.py` (`open_pr`: drop `--draft`)
- Modify: `.github/pull_request_template/new-build.md`
- Test: `tests/test_watch.py`

**Interfaces:** Produces: watcher PRs are ready, not draft. The template keeps the `{version}`, `{reference}`, `{mod_version}` and `{evidence}` fields.

- [ ] **Step 1: Write the failing test.** Change `run()`'s fake `gh` to also append its argv to `cwd.parent / 'gh.log'`, then:

```python
def test_watch_opens_ready_pr(checkout, dll_9807, msdl):
    p, _ = run(checkout, dll_9807, msdl)
    assert p.returncode == 0, p.stdout + p.stderr
    log = (checkout.parent / 'gh.log').read_text()
    assert 'pr create' in log and '--draft' not in log
```

- [ ] **Step 2: Bring over the test fix, then run the new test.** `git cherry-pick 0015faa`: it's the "stop assuming kKnownBuilds holds only 9807" fix from PR #3's branch. Without it, the whole suite fails on any second build. Then run `.venv/bin/pytest tests/test_watch.py::test_watch_opens_ready_pr -q`. Expected: FAIL (`--draft` is in the log).
- [ ] **Step 3: Remove `--draft` from `open_pr` and update the docstring.** Rewrite the template's "Before merging" section so it says:
  - this PR releases itself as v{mod_version} once both CI checks pass, and installs itself in the owner's Windhawk;
  - to stop that, convert it to draft or close it;
  - an optional checklist remains for a hand test.
  
  Remove the "Tested on OS build" line and the `release.sh` instruction.
- [ ] **Step 4: Run** `.venv/bin/pytest tests/test_watch.py -q` from a detached HEAD (see the note at the end of the plan). Expected: PASS.
- [ ] **Step 5: Commit** `feat(watcher): open build PRs ready, since they now release themselves`

---

### Task 3: Trust check for a build PR

**Files:**
- Create: `tools/check_build_pr.py`
- Test: `tests/test_check_build_pr.py`

**Interfaces:**
- Produces: `problems(repo: str, base: str, head: str, signing_key: str) -> list[str]`, which returns an empty list when the PR is trustworthy. `signing_key` is the value of `user.signingkey`: a path to a `.pub` file, or a private key path (as in CI). For a private key, derive the public key with `ssh-keygen -y -f`.
- CLI: `python tools/check_build_pr.py BASE HEAD`. It reads `user.signingkey` from git config, prints each problem on its own line, and exits 1 if there are any, 0 if not.

Checks, in order:
1. **Signatures:** write a temporary allowed-signers file `* namespaces="git" <pubkey>`. Every commit in `base..head` must show `%G?` = `G` under `git -c gpg.ssh.allowedSignersFile=<tmp> log`.
2. **Entries:** `known_builds()` on `head:src/...` minus `known_builds()` on `base:src/...` is exactly one entry, and no existing entry changed or disappeared.
3. **Version:** `@version` at head is the next minor after base's (`X.(Y+1).0`).
4. **README:** `git diff base head -- README.md` adds exactly one line, removes none, and the added line starts with `| {new entry's version} |`.

- [ ] **Step 1: Write the failing tests.** Build each fixture in a clone of `REPO` using `matcher.apply.apply_entry` with the `FAKE` entry from `tests/test_apply_verify.py` (move `FAKE` to `tests/conftest.py`). Commit with the CI or owner signing key that git is configured with.

```python
def test_clean_build_pr_passes(build_pr):        # apply_entry + signed commit
    assert problems(*build_pr, key()) == []

def test_unsigned_commit_refused(build_pr_unsigned):   # commit with -c commit.gpgsign=false
    assert any('not signed by this PC' in p for p in problems(*build_pr_unsigned, key()))

def test_other_key_refused(build_pr, tmp_path):        # a fresh ssh-keygen key as signing_key
    assert any('not signed by this PC' in p for p in problems(*build_pr, other_key(tmp_path)))

def test_two_entries_refused(build_pr_two):
    assert 'adds 2 builds, expected 1' in problems(*build_pr_two, key())

def test_changed_entry_refused(build_pr_edit_9807):    # also edits the 9807 row's unhide RVA
    assert 'changes existing build 10.0.26100.9807' in problems(*build_pr_edit_9807, key())

def test_version_skip_refused(build_pr_minor_plus_2):
    assert any(p.startswith('version ') for p in problems(*build_pr_minor_plus_2, key()))

def test_readme_extra_line_refused(build_pr_readme_extra):
    assert 'README.md must add exactly one build row' in problems(*build_pr_readme_extra, key())
```
Here `key()` returns `git config user.signingkey`, and `build_pr` yields `(repo, base_sha, head_sha)`.

- [ ] **Step 2: Run** `.venv/bin/pytest tests/test_check_build_pr.py -q`. Expected: FAIL (module missing).
- [ ] **Step 3: Implement `problems()` and the CLI in `tools/check_build_pr.py`,** returning the exact messages the tests assert on.
- [ ] **Step 4: Run** the same command. Expected: PASS.
- [ ] **Step 5: Commit** `feat: trust check for watcher build PRs`

---

### Task 4: `release.sh --auto`

**Files:**
- Modify: `tools/release.sh`
- Test: `tests/test_release_guard.py`

**Interfaces:**
- Consumes: `tools/check_build_pr.py BASE HEAD` (Task 3).
- Produces: `tools/release.sh <version> --pr N --auto --sha SHA`. The caller supplies the SHA, so the release is pinned to it. Exit codes: 0 released; 1 refused (message on stderr); 2 usage.

Behaviour with `--auto`. Everything else in the script is unchanged.
- **Usage:** requires `--pr` and `--sha`, refuses `--os-build`, and exits 2 on misuse.
- **Kept from `--pr`:** the draft refusal, the fork, base and merge refusals, the path checks, the version check, signing availability and resume.
- **Skipped:** the unticked-checklist check, the tested-OS-build requirement, the `read -p … yes` prompt and `$EDITOR`.
- **Pinned SHA:** `headRefOid` must equal `--sha`, or exit 1 with `PR #N moved`.
- **Trust check:** after the existing path checks, run `.venv/bin/python tools/check_build_pr.py "$BASE" "$SHA"` and exit 1 on failure.
- **Notes:** build the changes file from the PR's head commit message (`taskbar.dll X`, `Matched against Y`). It holds the single line from Global Constraints, passed as `release_notes.py --changes`.
- **Close comment:** `Released automatically in v$V once CI passed.`

- [ ] **Step 1: Write the failing tests.** Extend `run_release` with an `args` parameter. Make the fake `gh` log its argv to `tmp/gh.log`, and answer `release create` with exit 0.

```python
def test_auto_refuses_draft(setup): ...            # pr_json(draft=True) -> rc 1, 'still a draft', main not moved
def test_auto_ignores_checklist(setup): ...         # body with '- [ ]' -> rc 0
def test_auto_refuses_moved_sha(setup): ...         # --sha differs from headRefOid -> rc 1, 'moved'
def test_auto_refuses_untrusted(setup): ...         # unsigned commit -> rc 1, 'not signed by this PC'
def test_auto_releases(setup): ...                  # rc 0; origin main has signed commits (%G? == G);
                                                    # signed tag v<next minor>; gh.log has 'release create'
                                                    # and the notes file contains 'Not tested by hand.'
def test_auto_resumes_after_tag(setup): ...         # first run: fake gh fails 'release create' -> rc != 0, tag pushed;
                                                    # second run: rc 0, 'resuming', one tag, 'release create' logged twice
def test_auto_needs_sha(setup): ...                 # --auto without --sha -> rc 2
```
The happy path uses a real watcher-shaped PR: `apply_entry` with `FAKE`, a signed commit, and `@version` at the next minor.

- [ ] **Step 2: Run** `.venv/bin/pytest tests/test_release_guard.py -q`. Expected: the new tests FAIL, the existing ones pass.
- [ ] **Step 3: Implement** the `--auto` changes above in `tools/release.sh`. Keep the header comment accurate.
- [ ] **Step 4: Run** the same command. Expected: all PASS.
- [ ] **Step 5: Commit** `feat(release): --auto releases a trusted build PR without prompts`

---

### Task 5: Watcher releases pending build PRs

**Files:**
- Create: `tools/autorelease.py`
- Modify: `tools/watch_build.py` (`main`)
- Test: `tests/test_autorelease.py`

**Interfaces:**
- Consumes: `release.sh --auto` (Task 4); `notify`, `notify_once`, `git`, `REPO` from `watch_build`.
- Produces:
  - `BuildPR = namedtuple('BuildPR', 'number branch sha created draft')`
  - `pending_build_prs() -> list[BuildPR]`: `gh pr list --state open --json number,headRefName,headRefOid,createdAt,isDraft,isCrossRepository`, keeping same-repo PRs whose `headRefName` starts with `build/`, sorted oldest first.
  - `ci_state(sha: str) -> str`: one of `'success' | 'pending' | 'failure'`, from `gh api repos/{owner}/{repo}/commits/{sha}/check-runs`, considering only the two required names. Both completed and successful gives `success`; either one failed, cancelled or timed out gives `failure`; anything else, including a missing check, gives `pending`.
  - `wait_for_ci(sha: str, timeout: float) -> str`: polls `ci_state` every `CI_EVERY` seconds (env, default 20) until the result is no longer pending or the timeout passes. The timeout comes from env `CI_WAIT`, default 900.
  - `release(pr: BuildPR) -> str`: returns the released version. It clones `REPO` into a temp dir, sets the clone's `origin` URL to `REPO`'s origin URL, symlinks `.venv`, runs `tools/release.sh <version> --pr N --auto --sha SHA` there, and removes the clone. Raises `ReleaseRefused(stderr)` on exit 1.
  - `release_pending(opened: set[int]) -> list[str]`: returns the released versions. It handles one PR at a time, oldest first: skip drafts, wait on PRs in `opened`, check the others once, then release. After a release, it closes every other open `build/*` PR with `gh pr close N --delete-branch --comment "Superseded by v{V}; the watcher reopens it from the new main."`.
  - `watch_build.main`: after the build checks, runs `release_pending(opened)`. If anything was released and a PR was closed, it fetches and runs the build checks once more, then releases any PR that pass opened. `check()` returns the number of a PR it opened, so `main` can collect `opened`.
- **Toasts:**
  - CI failure: `notify_once(f'ci:{sha}', f'Taskbar {build}: CI failed', url)`
  - Refused: `notify_once(f'refused:{sha}', …, first stderr line)`
  - Released: `notify(f'v{V} released for taskbar {build}', release url)`. Task 6 later rewords this to "released and installed".
- **Network:** a `gh` or `git` network error while listing PRs or reading CI is printed and skipped, with no toast and exit 0.

- [ ] **Step 1: Write the failing tests.** Use a scripted fake `gh` (a Python script on `PATH`) that answers `pr list`, `api …check-runs`, `pr view` and `release create` from a JSON file the test writes, and logs every call. The origin is a throwaway bare repo, as in `test_watch.checkout`.

```python
def test_green_pr_released(...)                  # released == ['<next>']; origin has tag; 'release create' logged
def test_pending_pr_left(...)                    # check-runs in_progress, not in opened -> no release, rc 0, no toast
def test_failed_ci_toasts_once(...)              # two runs -> one 'CI failed' line in notify.log
def test_draft_never_released(...)               # isDraft true + green -> no release
def test_fork_pr_ignored(...)                    # isCrossRepository true -> not listed
def test_ci_unreachable_leaves_pr(...)           # fake gh api exits 1 -> rc 0, notify.log empty
def test_autorelease_leaves_checkout_alone(...)  # owner checkout on branch 'wip' with a dirty file:
                                                 # same branch, HEAD and file contents after release
def test_second_pr_reopened_after_release(...)   # two green PRs (9807 and 9974 fixtures):
                                                 # older released, newer closed with 'Superseded', new PR opened
```

- [ ] **Step 2: Run** `.venv/bin/pytest tests/test_autorelease.py -q`. Expected: FAIL (module missing).
- [ ] **Step 3: Implement** `tools/autorelease.py` and the `main`/`check` changes in `tools/watch_build.py`. Update the docstring and exit codes: a refused release exits 8.
- [ ] **Step 4: Run** `.venv/bin/pytest tests -q` from a detached HEAD. Expected: PASS.
- [ ] **Step 5: Commit** `feat(watcher): release build PRs once CI passes`

---

### Task 6: Install the released version into the owner's Windhawk

**Files:**
- Create: `tools/windhawk_install.py` (unelevated: layout check, compile, staging, trigger, wait)
- Create: `tools/install-mod.ps1` (elevated: validate, copy, registry)
- Modify: `tools/install-watch-task.ps1` (register `WindhawkTaskbarModInstall`; the script now needs an elevated prompt)
- Modify: `tools/autorelease.py` (call the install after a release, then toast)
- Modify: `.github/workflows/ci.yml` (Windows job runs the PowerShell test)
- Test: `tests/test_windhawk_install.py`, `tests/install-mod.Tests.ps1`

**Interfaces:**
- Consumes: `release_pending` (Task 5) and the Task 1 result (`RELOAD`).
- Produces (Python):
  - `layout_problems(windhawk_dir: str, data_dir: str, reg) -> list[str]`, where `reg(key) -> dict[str, str]` wraps `reg.exe query`. Expected layout:
    - `windhawk.ini` has `Portable=0`, `AppDataPath=%ProgramData%\Windhawk` and `RegistryKey=HKLM\SOFTWARE\Windhawk`;
    - `{data}\ModsSource\local@taskbar-autohide-any-edge.wh.cpp` and `{data}\Engine\Mods\64\` exist;
    - the mod key has `Version` and `LibraryFileName`, and that DLL exists.
  - `stage(version: str, staging: str) -> None`: runs `git show v{version}:src/…` into `staging/mod.wh.cpp`, `tools/compile-mod.sh staging/mod.wh.cpp staging/mod.dll`, and writes `staging/request.json` `{"version": V}`. Any old `result.json` is removed first.
  - `install(version: str) -> None`: layout check, then `stage`, then `schtasks.exe /run /tn WindhawkTaskbarModInstall`, then waits up to 120 s for `staging/result.json`. Raises `InstallError(msg)` on any problem.
  - Staging is `%LOCALAPPDATA%\taskbar-autohide\install`, resolved through `cmd.exe /c echo %LOCALAPPDATA%` and `wslpath`. Env overrides: `WINDHAWK_DIR`, `WINDHAWK_DATA`, `INSTALL_STAGING`, `REG_CMD`, `SCHTASKS_CMD`.
- Produces (PowerShell): `install-mod.ps1 [-Staging <dir>] [-DataDir <dir>] [-RegKey <psPath>]`. Defaults are the real paths and `HKLM:\SOFTWARE\Windhawk\Engine\Mods\local@taskbar-autohide-any-edge`.
  - **Refuses** (writes `{"ok":false,"error":…}` to `result.json`, changes nothing) when: `request.json`, `mod.dll` or `mod.wh.cpp` is missing or empty; the version is not `\d+\.\d+\.\d+`; the version is not newer than the registry `Version`; or the source's `// @version` differs from the request.
  - **Otherwise:**
    1. copy `mod.dll` to `Engine\Mods\64\local@taskbar-autohide-any-edge_{V}_{6 random digits}.dll`;
    2. copy `mod.wh.cpp` to `ModsSource\local@taskbar-autohide-any-edge.wh.cpp`;
    3. set `Version`, then `LibraryFileName`;
    4. if `RELOAD = restart`, run `windhawk.exe -restart -tray-only`;
    5. write `{"ok":true,"dll":…}`.
- `WindhawkTaskbarModInstall` task: action `powershell.exe -NoProfile -ExecutionPolicy Bypass -File <repo>\tools\install-mod.ps1` (repo path via `\\wsl.localhost\…`), `-RunLevel Highest`, the current user as principal, no triggers, `MultipleInstances IgnoreNew`, a 5-minute limit. `install-watch-task.ps1` starts with `#Requires -RunAsAdministrator`, and `-Uninstall` removes both tasks.
- **After the release** (in `release_pending`): call `install(V)`. On success, toast `v{V} released and installed for taskbar {build}`. On `InstallError`, toast `v{V} released; install it in Windhawk by hand` with the message, using `notify_once(f'install:{V}', …)`. The release itself still counts as done.

- [ ] **Step 1: Write the failing Python tests** against a fake `WINDHAWK_DIR`/`WINDHAWK_DATA` tree, a fake `reg` (a dict), and a fake `SCHTASKS_CMD` that writes `result.json`:

```python
def test_layout_ok(fake_windhawk): assert layout_problems(*fake_windhawk) == []
def test_layout_portable_refused(fake_windhawk): ...   # Portable=1 -> ['Windhawk is in portable mode']
def test_layout_missing_key_refused(fake_windhawk): ... # reg returns {} -> problem names the key
def test_stage_compiles_tag(...): ...                  # skip unless Windhawk's compiler is installed (as test_mod_guard)
def test_install_reports_task_error(...): ...          # fake task writes ok:false -> InstallError(message)
def test_install_times_out(...): ...                   # fake task writes nothing, INSTALL_WAIT=1 -> InstallError('timed out')
def test_release_then_install_toasts(...): ...         # Task 5 harness: notify.log has 'released and installed'
def test_install_failure_still_released(...): ...      # tag exists; notify.log has 'install it in Windhawk by hand'
```

- [ ] **Step 2: Write the failing PowerShell tests** (`tests/install-mod.Tests.ps1`, plain PowerShell with `throw` on failure and no Pester). They run against a temp `-DataDir` and `-RegKey HKCU:\Software\TaskbarAutohideTest\mod`, which is created with `Version=1.18.0`, `LibraryFileName`, `Disabled=1`, `LoggingEnabled=1` and a `Settings` subkey holding one value:
  - `Install keeps Disabled and Settings`: after the install, `Version=1.19.0`, `LibraryFileName` matches `_1.19.0_\d{6}\.dll` and the file exists, `Disabled` is still 1, and the `Settings` value is unchanged.
  - `Refuses older version`: request 1.17.0 gives `ok:false` and leaves the registry unchanged.
  - `Refuses source version mismatch`: gives `ok:false`.
  - `Refuses empty dll`: gives `ok:false`.
  - The tests delete the HKCU test key afterwards.

- [ ] **Step 3: Run** `.venv/bin/pytest tests/test_windhawk_install.py -q`, and `powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w tests/install-mod.Tests.ps1)"`. Expected: both FAIL.
- [ ] **Step 4: Implement** `windhawk_install.py`, `install-mod.ps1`, the task registration, and the hook in `autorelease.py`.
- [ ] **Step 5: Add the CI step** in the `compile` job: `- run: powershell -NoProfile -ExecutionPolicy Bypass -File tests/install-mod.Tests.ps1`, with `shell: pwsh`.
- [ ] **Step 6: Run** both test commands and `.venv/bin/pytest tests -q`. Expected: PASS.
- [ ] **Step 7: Commit** `feat: install released builds into the owner's Windhawk`

---

### Task 7: Docs, re-register the tasks, end-to-end on 26100.9974

**Files:**
- Modify: `README.md` (only if it describes the owner's release flow; check first), `docs/ROADMAP.md`, the spec's "Open question" section (the Task 1 result), `tools/release.sh` header comment
- Commit: the spec and this plan, if not committed yet

- [ ] **Step 1: Update the docs** to describe the hands-off flow and the stop signal (draft or close). Commit `docs: hands-off releases for new taskbar builds`.
- [ ] **Step 2: Push the branch and open a PR.** Once CI passes, land it as the earlier tooling work was landed: rebase onto main with signed commits and fast-forward-push main. That's the owner's call, and an admin bypasses the CI rule. No release is needed, because the mod source is unchanged and stays at 1.18.0. (`release.sh --pr` can't be used: it refuses paths outside `src/` and `README.md`.)
- [ ] **Step 3: The owner runs** `.\tools\install-watch-task.ps1` from an elevated PowerShell. Check: `Get-ScheduledTask WindhawkTaskbarBuildWatch, WindhawkTaskbarModInstall` lists both.
- [ ] **Step 4: Recut PR #3 (26100.9974).** Its branch carries the test-fix commit, which touches `tests/` and so fails the path check. It's also a draft. Close it with `--delete-branch`, then run `tools/watch-build.sh`. The watcher matches the build again from the new main, opens a ready PR, waits for CI and releases it. Expected: a toast `v… released and installed for taskbar 10.0.26100.9974`, a GitHub release with "Not tested by hand.", and `tasklist.exe /m` showing the new DLL in explorer.exe.

---

**Note on running `tests/test_watch.py` locally:** its fixture bare-clones this repo, including local branches. Run it with no local `build/*` branches, or from a detached HEAD on a clone without them. Otherwise six tests fail for reasons unrelated to the change.
