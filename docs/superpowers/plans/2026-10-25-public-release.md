# Going public: history cleanup, build automation and releases — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the repo safe to publish, then make it cheap to support taskbar builds whose PDBs Microsoft has not published, and give each mod version a GitHub release that says which Windows builds it works with.

**Architecture:**
- Part 1 rewrites the history once, with `git filter-repo` plus a signed rebase, and recreates the GitHub repo.
- Part 2 adds a Python package, `tools/matcher/`. It finds the seven hooked functions in a new `taskbar.dll` by anchors, confirms each by similarity to a reference build that has a PDB, and fails closed.
- A Windows scheduled task runs the matcher at sign-in and opens a draft PR.
- A cloud routine verifies the table once real PDBs appear.
- Part 3 adds compatibility data, a notes generator and a local release script.

**Tech Stack:** Python 3.14 + capstone 5 + pytest (in `.venv`), git-filter-repo (in `~/.local/bin`), bash, PowerShell 7 / Windows Task Scheduler, `gh`, the RemoteTrigger API.

**Spec:** `docs/superpowers/specs/2026-10-25-public-release-design.md`

## Global Constraints

- Every commit and tag is signed with the owner's SSH key (`gpg.format=ssh`, `commit.gpgsign=true`, `tag.gpgsign=true`). If signing fails, stop and report it. Never use `--no-gpg-sign` or `-c commit.gpgsign=false`.
- Force-push and deleting the GitHub repo are allowed only in Task 3. The repo delete needs the owner's confirmation immediately beforehand. Never push to a remote named `upstream`.
- The recreated repo is private: `gh repo create --private`.
- Never put a `https://claude.ai/code/session_…` URL in a commit, PR, issue or release. Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and no `Claude-Session` line.
- Never commit or upload a Microsoft DLL or PDB, or any dump of one.
- Matcher outputs: a `kKnownBuilds` entry is only produced when every one of the seven functions passes. Otherwise it exits non-zero with a reason.
- Nothing merges automatically. Releasing is `tools/release.sh`, run by the owner.
- Personal-information patterns (Windows username, machine name, game paths) live only in the out-of-repo cleanup directory `~/windhawk-cleanup/`, never in the repo.

## Review Focus

- **Matcher run on the build it is already in the table for:** must report "already known" and open nothing. Pinned in Task 8 (`watch-build.sh --dry-run` on 9807).
- **The PDB is published by the time the task runs:** the watcher must do nothing, because symbols win. Pinned in Task 8 (`test_watch_skips_when_pdb_published`).
- **Signing agent not loaded at sign-in:** the watcher must not commit unsigned. It notifies "signing unavailable, run bin/keys-load.sh" and stops. Pinned in Task 8 (`test_watch_stops_without_signing`).
- **Reference build older than the previous one, or several references available:** the matcher picks the newest version below the new build that has a PDB. Pinned in Task 6 (`test_pick_reference_newest_below`).
- **The rewrite silently drops or alters real content:** the tree at the rewritten HEAD must equal the original HEAD tree, apart from the generalised text. Pinned in Task 2 step 5.

---

## Part 1: History cleanup (do first; nothing else may be committed until Task 3 is done)

### Task 1: Backup and baseline scan

**Files:**
- Create (outside repo): `~/windhawk-cleanup/backup-2026-10-25.bundle`, `~/windhawk-cleanup/scan.py`, `~/windhawk-cleanup/patterns.local`, `~/windhawk-cleanup/baseline.txt`

**Interfaces:**
- Produces: `python3 ~/windhawk-cleanup/scan.py <repo>` prints one line per category, `<category>: <hits>`, and exits 1 if any category in the must-be-zero set is non-zero.

- [ ] **Step 1:** Commit the uncommitted spec and this plan first, signed (`docs: design and plan for going public`), so they are part of the history that gets rewritten.
- [ ] **Step 2:** Run `git bundle create ~/windhawk-cleanup/backup-2026-10-25.bundle --all`, then `git bundle verify` on it. Expected: "The bundle records a complete history".
- [ ] **Step 3:** Write `scan.py`. It scans `git log --all --format=%B -p --text` plus all tag messages. Categories:
  - **Must be zero:** session URL, non-noreply email, Windows username, machine name, `C:\Users\<name>`, `/mnt/c/Users/<name>`, `/home/<name>`, IPv4, token shapes, drive-letter game-library paths, the GPU-vendor process name, and any blob path matching `*pdb-strings*`.
  - **Report only:** everything else.

  The literal values (the owner's Windows username, machine name, game-library roots and the GPU-vendor process name) are read from `patterns.local` and never written into the repo.
- [ ] **Step 4:** Run it and save the output to `baseline.txt`. Expected: session URL 58; game paths > 0; GPU-vendor process > 0; pdb-strings 0 (the dumps are git-ignored and were never committed); every other must-be-zero category 0. Exit code 1.

### Task 2: Rewrite and re-sign

**Files:**
- Create (outside repo): `~/windhawk-cleanup/replacements.txt`, `~/windhawk-cleanup/rewrite.sh`
- Work in a fresh clone: `~/windhawk-cleanup/work/` (`git clone --no-local ~/windhawk-taskbar-autohide work`)

**Interfaces:**
- Consumes: `scan.py` (Task 1).
- Produces: `~/windhawk-cleanup/work` with a rewritten, fully signed `main` and 28 signed annotated tags (`v1.0.0`…`v1.15.0` plus `v1.16.0` on `8124582`'s rewrite and `v1.17.0` on `5579216`'s rewrite).

- [ ] **Step 1:** Write `replacements.txt` in filter-repo `--replace-text` syntax, built from `patterns.local`:
  - Every drive-letter game-library path (Steam, Origin and Battle.net roots, found with `git log --all -p -S SteamLibrary`) becomes `<GameDrive>:\<Library>\*`.
  - Every mention of the GPU-vendor process becomes `<other-app>.exe`.
  - The single-line registry `Exclude` value becomes the generalised list.
- [ ] **Step 2:** In `rewrite.sh`, record a tag map before rewriting: `tag → (author date ISO, subject)` for every tag. Check this pair is unique per commit; it is, since there are 0 duplicate `date|subject` pairs and 0 merges.
- [ ] **Step 3:** Run `git filter-repo --force --replace-text replacements.txt --message-callback '…'`. The callback deletes lines matching `^Claude-Session: https://claude\.ai/code/session_\S+\s*$` plus any trailing blank line it leaves.
- [ ] **Step 4:** Re-sign: `git rebase --root --committer-date-is-author-date --gpg-sign`. Then recreate each tag from the map with `git tag -s -f -m "<original tag message>"` on the commit whose `(author date, subject)` matches. Add `v1.16.0` (message `v1.16.0`) and `v1.17.0` (message `v1.17.0`) on the rewrites of `8124582` and `5579216`.
- [ ] **Step 5: Verify, all must hold:**
  - `scan.py work` exits 0.
  - `git log --format=%G? | sort -u` prints only `G`.
  - `git tag -v` succeeds for all 28 tags.
  - `git rev-list --count HEAD` = 200 plus Task 1's commit.
  - `git diff <original HEAD> HEAD --stat` against the backup touches only the files containing the generalised text.
  - Author dates are identical to the original's in order: compare `git log --format=%ad` before and after.
- [ ] **Step 6:** Show the owner `scan.py` before and after, and the `--stat` from step 5. Do not proceed without an OK.

### Task 3: Recreate the GitHub repo and push

**Interfaces:**
- Consumes: the rewritten `~/windhawk-cleanup/work` (Task 2).
- Produces: `github.com/merson316/windhawk-taskbar-autohide`, private, holding only the clean history and tags. Local `~/windhawk-taskbar-autohide` is reset to it.

- [ ] **Step 1:** Ask the owner to confirm deleting the GitHub repo. State that it has 0 issues, PRs, releases and forks (re-check with `gh api`), and that the backup bundle exists.
- [ ] **Step 2:** On OK, run `gh repo delete merson316/windhawk-taskbar-autohide --yes`. Then run `gh repo create merson316/windhawk-taskbar-autohide --private --description "Windhawk mod: makes Windows 11 taskbar auto-hide work on the top, left and right edges"`.
- [ ] **Step 3:** From `work`, run `git push origin main --tags`, with `origin` set to the new repo. Then point `~/windhawk-taskbar-autohide` at it: `git fetch origin && git reset --hard origin/main` (after confirming its working tree is clean) and `git fetch --tags --force`.
- [ ] **Step 4:** Ask the owner to re-grant the Claude GitHub app on the new repo if needed. Run the symbol-watch routine once with RemoteTrigger `run`. Expected: the run log ends "Not published yet (404)" (or a PDB report).
- [ ] **Step 5:** Re-run `scan.py` on a fresh `git clone` of the remote. Expected: exit 0.

---

## Part 2: Matcher automation

### Task 4: PE and PDB readers

**Files:**
- Create: `tools/matcher/__init__.py`, `tools/matcher/pe.py`, `tools/matcher/pdb.py`, `tools/requirements.txt` (`capstone>=5,<6`, `pytest>=8`), `tests/conftest.py`, `tests/test_pe_pdb.py`
- Modify: `.gitignore` (add `.venv/`, `__pycache__/`, `.pytest_cache/`)

**Interfaces:**
- `pe.PE(path: str)` provides:
  - `.image_base: int`, `.timestamp: int`, `.size_of_image: int`
  - `.codeview() -> tuple[uuid.UUID, int]` (GUID, age)
  - `.pdb_key() -> str`, the msdl GUID+age key, for example `1AF71656D4C04FBD463BFF7E4ED169CE1`
  - `.dll_key() -> str`, timestamp `%08X` + size `%x`
  - `.read(rva: int, n: int) -> bytes`
  - `.sections`
  - `.functions() -> list[tuple[int, int]]`, the (start RVA, end RVA) pairs from `.pdata`, merged so chained unwind entries belong to their parent
  - `.imports() -> dict[int, str]`, IAT slot RVA to `dll!name`
  - `.find_string(s: str, wide: bool) -> list[int]`, the RVAs of NUL-terminated occurrences
  - `.file_version() -> str`
- `pdb.publics(pdb_path: str, names: list[str]) -> dict[str, int]`, mangled name to RVA, segment 1 only (RVA = 0x1000 + offset), found by the S_PUB32 scan: kind `0x110E` 12 bytes before the name.
- `tests/conftest.py` fixtures: `dll_9278`, `dll_9549` and `dll_9807` (paths under `/mnt/c/Windows/WinSxS/amd64_microsoft-windows-taskbar-dll_31bf3856ad364e35_10.0.26100.<ver>_none_*/Taskbar.dll`), and `pdb_9278` and `pdb_9549` (`/mnt/c/ProgramData/Windhawk/Engine/Symbols/Taskbar.pdb/<KEY>/Taskbar.pdb`). Each calls `pytest.skip` when its file is missing.
- Module constant `HOOKED` in `tools/matcher/__init__.py`, an ordered dict of field to mangled name:
  - `getStuckPlace`: `?GetStuckPlace@TrayUI@@UEAAIXZ`
  - `getAutoHideFlags`: `?GetAutoHideFlags@TrayUI@@UEAAIXZ`
  - `setAutoHideFlags`: `?SetAutoHideFlags@TrayUI@@UEAAXI@Z`
  - `getDockedRect`: `?GetDockedRect@TrayUI@@UEAAIPEAUtagRECT@@H@Z`
  - `unhide`: `?Unhide@TrayUI@@UEAAXW4TrayUnhideFlags@TrayCommon@@W4UnhideRequest@3@@Z`
  - `slideWindow`: `?SlideWindow@TrayUI@@UEAAXPEAUHWND__@@PEBUtagRECT@@PEAUHMONITOR__@@_N3@Z`
  - `permitAutoHide`: `?PermitAutoHide@CTaskListWnd@@UEAAHXZ`

  The field order matches `KnownBuild` in the mod.

- [ ] **Step 1:** Create `.venv`: `python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt`.
- [ ] **Step 2: Write failing tests:**
  - `test_codeview_9807`: `pdb_key()` = `1AF71656D4C04FBD463BFF7E4ED169CE1`; `dll_key()` = `7BA9A53231a000`; `file_version()` = `10.0.26100.9807`.
  - `test_publics_9549`: the result equals `{getAutoHideFlags: 0x16500, setAutoHideFlags: 0x91910, slideWindow: 0x5D760, getStuckPlace: 0x6B5E0, getDockedRect: 0x16460, unhide: 0x924C0, permitAutoHide: 0x61900}`, with names mapped through `HOOKED`.
  - `test_publics_9278`: `getAutoHideFlags` = 91104, `setAutoHideFlags` = 599328, `slideWindow` = 389216, `getStuckPlace` = 445984, `getDockedRect` = 90944, `permitAutoHide` = 406544.
  - `test_functions_contains`: in 9807, `.functions()` has a range starting at 0x65250 of length 18, and one starting at 0x3CC90 of length 176.
  - `test_find_string`: `find_string("IsAutoHideEnabled", wide=True)` on 9807 returns exactly one RVA. (Wide or narrow is for the implementer to verify; record which in the test.)
- [ ] **Step 3:** Run `.venv/bin/pytest tests/test_pe_pdb.py -q`. Expected: fails with ImportError.
- [ ] **Step 4:** Implement `pe.py` and `pdb.py` (standard library plus `struct` only).
- [ ] **Step 5:** Run the tests. Expected: 5 passed.
- [ ] **Step 6:** Commit: `feat(matcher): PE and PDB readers`.

### Task 5: Disassembly, normalisation and similarity

**Files:**
- Create: `tools/matcher/code.py`, `tests/test_code.py`

**Interfaces:**
- `code.Function(pe: PE, start: int)` provides:
  - `.end`
  - `.size`
  - `.insns`: the capstone instructions, x64
  - `.calls() -> list[int]`: direct call targets (RVA)
  - `.import_calls() -> set[str]`: `dll!name` through the IAT
  - `.rip_refs() -> list[int]`: RIP-relative targets
  - `.member_offsets() -> set[int]`: displacements on non-RSP/RBP base registers
  - `.normalised() -> list[str]`: mnemonic plus operands, with absolute and RIP-relative addresses replaced by `ADDR` and call targets by `CALL`
  - `.block_count`
- `code.similarity(a: Function, b: Function) -> float`: `difflib.SequenceMatcher(None, a.normalised(), b.normalised(), autojunk=False).ratio()`

- [ ] **Step 1: Write failing tests:**
  - `test_identical_functions`: 9549 `0x91910` against 9807 `0x3CC90` gives similarity 1.0, the same `import_calls()` (containing `SetPropW`) and the same `member_offsets()`.
  - `test_close_functions`: `slideWindow` 9549 `0x5D760` against 9807 `0x41260` gives similarity ≥ 0.90; `unhide` `0x924C0` against `0x5E670` ≥ 0.88; `permitAutoHide` `0x61900` against `0x35C40` ≥ 0.90.
  - `test_unrelated_functions`: 9549 `unhide` against 9807 `0xBAE8C` (the `secondarytray.cpp` one) gives < 0.5.
- [ ] **Step 2:** Run them. Expected: fail.
- [ ] **Step 3:** Implement `code.py`.
- [ ] **Step 4:** Run them. Expected: 3 passed. Record the observed scores in the test file's docstring; they feed Task 6's thresholds.
- [ ] **Step 5:** Commit: `feat(matcher): normalised disassembly and similarity`.

### Task 6: Anchors and the matcher CLI

**Files:**
- Create: `tools/matcher/anchors.py`, `tools/matcher/match.py`, `tools/match-build.py`, `tests/test_match.py`

**Interfaces:**
- `match.match_build(new: PE, ref: PE, ref_rvas: dict[str, int]) -> MatchResult`. `MatchResult` is a dataclass:
  - `rvas: dict[str, int] | None`: None on failure
  - `rows: list[Row]`: per field, the anchor name, candidate RVA, size, similarity, imports equal, offsets equal, verdict
  - `reason: str | None`
- `match.pick_reference(new: PE, candidates: list[tuple[PE, str]]) -> tuple[PE, str]`. Candidates are (DLL, PDB path) pairs; it picks the highest `file_version()` strictly below the new build's.
- `match.format_entry(new: PE, rvas: dict[str, int]) -> str`: the exact C++ initializer line for `kKnownBuilds`, in the layout of the existing 9807 entry (version string, GUID braces, age, seven `0x%X` values in `HOOKED` order).
- CLI: `tools/match-build.py --new DLL [--ref DLL --ref-pdb PDB | --auto-ref] [--json OUT] [--entry OUT]`.
  - Exit 0: prints the evidence table.
  - Exit 2: no match, with the reason on stderr.
  - `--auto-ref` gathers candidates from WinSxS `amd64_microsoft-windows-taskbar-dll_*` folders whose PDB key is present in the Windhawk symbol cache, otherwise downloadable from `https://msdl.microsoft.com/download/symbols/Taskbar.pdb/<KEY>/Taskbar.pdb` into `~/.cache/taskbar-matcher/`.
- Anchors, in `anchors.py`, one function per field returning candidate start RVAs. Each candidate is accepted only if similarity ≥ the threshold, `import_calls` are equal and `member_offsets` are equal (for `unhide`: equal after dropping stack displacements):

  | Field | Anchor |
  |---|---|
  | `getAutoHideFlags` | functions containing the shape the mod's `ScanForOffsets` accepts (both branch layouts) |
  | `getStuckPlace` | the exact 18 bytes `8B 81 1C 01 00 00 83 F8 FF 75 06 8B 81 10 01 00 00 C3` at a function start |
  | `setAutoHideFlags` | functions with a RIP reference to the wide string `IsAutoHideEnabled` |
  | `unhide` | functions with a RIP reference to `AutoHideOnTaskbarShow`; choose the most similar |
  | `getDockedRect` | functions with a direct call to the matched `getAutoHideFlags` |
  | `slideWindow` | functions with `44 8B B3 6C 01 00 00` at +0xA6 and `44 8B B3 68 01 00 00` at +0x123 |
  | `permitAutoHide` | functions containing `4C 8D 87 90 01 00 00 48 8D 97 98 01 00 00` and `48 8D 8F 40 02 00 00` |

  Each field must end with exactly one accepted candidate. Zero or more than one makes the result fail, with the reason naming the field.
- Thresholds are constants in `match.py`: `EXACT = 0.999` for `getStuckPlace` and `getAutoHideFlags`, `CLOSE = 0.85` for the rest. Adjust only if Task 5's observed scores show the 9278→9549 calibration needs it, and say so in the commit message.

- [ ] **Step 1: Write failing tests:**
  - `test_calibrate_9278_to_9549`: `match_build(9549, 9278, publics(9278))` returns `rvas` equal to `publics(pdb_9549)` for all seven fields.
  - `test_reproduce_9807`: `match_build(9807, 9549, publics(9549))` returns `{getStuckPlace: 0x65250, getAutoHideFlags: 0x41F70, setAutoHideFlags: 0x3CC90, getDockedRect: 0x41ED0, unhide: 0x5E670, slideWindow: 0x41260, permitAutoHide: 0x35C40}`.
  - `test_format_entry_9807`: `format_entry` equals the 9807 initializer text in `src/taskbar-autohide-any-edge.wh.cpp`, ignoring whitespace.
  - `test_fails_closed_missing_anchor`: a copy of 9807 bytes with the `IsAutoHideEnabled` string zeroed, loaded through a `PE` built from bytes (add `PE.from_bytes`), gives `rvas is None` and a `reason` containing `setAutoHideFlags`.
  - `test_pick_reference_newest_below`: with candidates 9278, 9549 and 9807 and new = 9807, it picks 9549; with new = 9549, it picks 9278.
- [ ] **Step 2:** Run them. Expected: fail.
- [ ] **Step 3:** Implement `anchors.py`, `match.py` and the CLI.
- [ ] **Step 4:** Run them. Expected: 5 passed. Also run `.venv/bin/python tools/match-build.py --new <9807> --auto-ref`. Expected: exit 0, 9549 chosen as the reference, the table printed with all seven rows "ok".
- [ ] **Step 5:** Commit: `feat(matcher): anchor-based function matching that fails closed`.

### Task 7: Applying an entry, and verifying it against a real PDB

**Files:**
- Create: `tools/matcher/apply.py`, `tools/verify-known-build.py`, `tests/test_apply_verify.py`

**Interfaces:**
- `apply.apply_entry(src_path, readme_path, entry_line: str, version: str, base_version: str) -> str`:
  - inserts `entry_line` (with a leading `// Matched against <base_version> by tools/match-build.py.` comment) as the last element of `kKnownBuilds`
  - adds a `| <version> | <base_version> |` row to the README table under "Requirements"
  - bumps `@version` minor (`1.17.0` becomes `1.18.0`)
  - returns the new mod version
  - refuses (ValueError) if an entry with the same GUID already exists
- `apply.known_builds(src_path) -> list[dict]`: parses `kKnownBuilds`, giving `version`, `pdb_key` and the seven RVAs.
- `tools/verify-known-build.py --src FILE --pdb PDB --version V`: prints a markdown table (function, expected, actual, match) and exits 0 if all match, otherwise 1. Used by the cloud routine (Task 9).

- [ ] **Step 1: Write failing tests:**
  - `test_known_builds_parses_9807`: there is one entry, with `pdb_key` `1AF71656D4C04FBD463BFF7E4ED169CE1` and the seven RVAs above.
  - `test_apply_to_temp_copy`: apply a fake entry (GUID all `0x11`, version `10.0.26100.9999`) to temp copies of the source and README. Then `known_builds` returns 2 entries, the source contains `@version         1.18.0`, the README contains `| 10.0.26100.9999 | 10.0.26100.9807 |`, and `tools/whcompile.sh <temp copy>` exits 0.
  - `test_apply_duplicate_refused`: applying the 9807 entry again raises ValueError.
  - `test_verify_against_9549_pdb`: building an entry from `publics(pdb_9549)` and verifying it against `pdb_9549` gives exit 0; changing one RVA gives exit 1 with the field marked `no`.
- [ ] **Step 2:** Run them. Expected: fail.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run them. Expected: 4 passed.
- [ ] **Step 5:** Commit: `feat(matcher): apply table entries and verify them against real PDBs`.

### Task 8: Sign-in watcher and scheduled task

**Files:**
- Create: `tools/watch-build.sh`, `tools/install-watch-task.ps1`, `.github/pull_request_template/new-build.md`, `tests/test_watch.py`

**Interfaces:**
- `tools/watch-build.sh [--dry-run]`, in WSL, from the repo root. In order:
  1. Read `/mnt/c/Windows/System32/taskbar.dll` (`pe.PE`).
  2. If its `pdb_key` is in `known_builds` → print `known: <version>` and exit 0.
  3. HEAD msdl for the PDB. If 200 or 302 → print `symbols published` and exit 0.
  4. If branch `build/<version>` exists locally or on origin → print `pending PR` and exit 0.
  5. Run the matcher (`--auto-ref`). On exit 2 → notify `"Taskbar <version>: automatic match failed — <reason>"` and exit 3.
  6. Check signing: `ssh-add -L` lists the key named in `user.signingkey`. If not → notify `"Taskbar <version> matched but signing is unavailable: run bin/keys-load.sh wsl, then tools/watch-build.sh"` and exit 4.
  7. `git switch -c build/<version> origin/main`, `apply_entry`, then a signed commit `feat: built-in addresses for taskbar.dll <version>` with the evidence table in the body.
  8. `git push -u origin build/<version>`, then `gh pr create --draft --title "taskbar.dll <version>: built-in addresses" --body-file <template with evidence filled in>`.
  9. Notify `"Taskbar <version>: draft PR #<n> ready to test"`.

  With `--dry-run`, steps 6–9 only print what they would do.
- Notifications go through `tools/toast.ps1`, extended with `-Title` and `-Message` parameters (default to the existing test text), invoked via `powershell.exe -NoProfile -File`.
- `install-watch-task.ps1`: registers the task `WindhawkTaskbarBuildWatch`.
  - Trigger: at logon of the current user, delayed 2 minutes.
  - Action: `wsl.exe -d <default distro> --cd ~/windhawk-taskbar-autohide -e bash -lc "tools/watch-build.sh >> ~/.cache/taskbar-matcher/watch.log 2>&1"`.
  - Supports `-Uninstall`.
- The PR template has the evidence table placeholder and a checklist:
  - [ ] Compiled this PR's `.wh.cpp` in Windhawk
  - [ ] Auto-hide works on a side edge
  - [ ] Windhawk log shows "using the built-in addresses"
  - Tested on OS build (winver): `_____`

- [ ] **Step 1: Write failing tests**, in `tests/test_watch.py`, running `watch-build.sh` with `TASKBAR_DLL`, `MSDL_BASE` and `NOTIFY_CMD` environment overrides:
  - `test_watch_known_build`: on real 9807 → exit 0, stdout `known: 10.0.26100.9807`.
  - `test_watch_skips_when_pdb_published`: with 9549 as `TASKBAR_DLL` against a temp source that lacks it, and real msdl → `symbols published`, exit 0.
  - `test_watch_dry_run_new_build`: with 9807 against a temp checkout whose 9807 entry was removed, `MSDL_BASE` set to a local stub server that returns 404, and `--dry-run` → exit 0. Output contains the 9807 entry line and `would open draft PR`.
  - `test_watch_stops_without_signing`: as above without `--dry-run`, with `SSH_AUTH_SOCK` set to an empty agent → exit 4, and the notify command was called with text containing `signing is unavailable`. No commit is created.
- [ ] **Step 2:** Run them. Expected: fail.
- [ ] **Step 3:** Implement the script, template, toast parameters and installer.
- [ ] **Step 4:** Run them. Expected: 4 passed. Then run `pwsh.exe -File tools\install-watch-task.ps1` from Windows. Expected: `Get-ScheduledTask WindhawkTaskbarBuildWatch` shows the task Ready. Run it once with `Start-ScheduledTask`; `watch.log` ends `known: 10.0.26100.9807`.
- [ ] **Step 5:** Commit: `feat: sign-in watcher that opens a draft PR for unpublished taskbar builds`.

### Task 9: Generalise the cloud verification routine

**Interfaces:**
- Consumes: `tools/verify-known-build.py` and `apply.known_builds` (Task 7).

- [ ] **Step 1:** Update the routine "Taskbar 9807 symbol watch" (RemoteTrigger `update`):
  - Rename it to "Taskbar built-in addresses verify".
  - Keep the 08:00 UTC cron.
  - Prompt: install `tools/requirements.txt`; list entries with `apply.known_builds('src/taskbar-autohide-any-edge.wh.cpp')`. For each entry: HEAD msdl for `pdb_key`. On 404, skip. On 302 or 200, skip if an issue titled `taskbar.dll <version> symbols published: built-in addresses check` exists; otherwise download the PDB to `/tmp` and run `verify-known-build.py`. Open that issue with its table and a verdict.
  - Final output: one line per entry.
  - Same no-session-URL and no-code-change rules as now.
- [ ] **Step 2:** Run it once. Expected: the run log ends with `10.0.26100.9807: not published yet (404)`.

---

## Part 3: Releases

### Task 10: Compatibility data, notes generator and release script

**Files:**
- Create: `docs/compatibility.json`, `tools/release_notes.py`, `tools/release.sh`, `tests/test_release_notes.py`

**Interfaces:**
- `docs/compatibility.json` is a list of `{"os_build": "26300.9807", "taskbar": "10.0.26100.9807", "support": "built-in" | "symbols", "tested_with": ["1.17.0"]}`.
- `release_notes.render(version: str, compat: list[dict], known: list[dict], changes: list[str]) -> str`. The markdown has these sections, in order:
  1. `## Works with`, containing a "Tested on" table (OS build, taskbar.dll, support) of the entries whose `tested_with` contains `version`.
  2. "Built-in support for builds without published symbols" (from `known`).
  3. The fixed sentence: `Other builds: works wherever Windhawk can download the taskbar's symbols, unless an update changes the code the mod checks; in that case the mod shows a warning instead of hooking.`
  4. `## Changes`, with the bullets.
  5. `## Install`: "Download `taskbar-autohide-any-edge.wh.cpp` below, then in Windhawk: Create a new mod → replace the contents → Compile → Enable."
- `release_notes.py --version V [--changes FILE]`: when `--changes` is absent, it lists the commit subjects since the previous tag as a draft for the owner to rewrite in plain language.
- `tools/release.sh <version> [--pr N]`:
  1. If `--pr` is given: `gh pr view N --json headRefName`, then fetch it, `git rebase` it signed onto `main`, and fast-forward `main`.
  2. Check that `@version` equals `<version>`.
  3. Push `main`.
  4. `git tag -s v<version> -m v<version>`, then push the tag.
  5. Write the notes to a temp file, open `$EDITOR` for the owner, then `gh release create v<version> --title "v<version>" --notes-file <file> src/taskbar-autohide-any-edge.wh.cpp`.
  6. Append `<version>` to the `tested_with` of the compatibility row for the PR's recorded OS build, and commit that signed (`docs: record <version> as tested on <os build>`).

  It refuses if the working tree is dirty or the tag exists.

- [ ] **Step 1: Write failing tests:**
  - `test_render_1_17_0`: for 1.17.0, the notes contain `| 26300.9807 | 10.0.26100.9807 | built-in |`, the built-in list contains `10.0.26100.9807`, and the fixed "Other builds" sentence appears verbatim.
  - `test_render_no_tested`: a version with no compatibility rows has no "Tested on" table, and still has the other sections.
- [ ] **Step 2:** Run them. Expected: fail.
- [ ] **Step 3:** Implement `release_notes.py` and `release.sh`. Seed `compatibility.json` with the 9807 row (`tested_with: ["1.17.0"]`); Task 11 fills the rest.
- [ ] **Step 4:** Run them. Expected: 2 passed. Run `bash -n tools/release.sh`. Expected: no output.
- [ ] **Step 5:** Commit: `feat: release notes with build compatibility, and a local release script`.

### Task 11: Backfill releases and hand over

- [ ] **Step 1: Derive a "Tested on" table** for `v1.15.0`, `v1.16.0` and `v1.17.0`.
  - Take each tag's date against the dates each taskbar.dll build arrived on this PC (from WinSxS: 9278 on 27 Aug, 9549 on 23 Sep, 9807 on 17 Oct).
  - Get the OS builds from the Windows update history (`Get-HotFix`, plus the `UBR` values Windows reports for those updates).
  - Version 1.16.0 was never run on 9807 with symbols, so it gets no "Tested on" row unless the owner says otherwise.
- [ ] **Step 2:** Show the owner the table and the plain-language change notes for each of the three versions. Wait for their OK and edits.
- [ ] **Step 3:** Write the confirmed rows into `compatibility.json` and commit them, signed. Then create the three releases with `gh release create` (tags already exist from Task 2), each with the `.wh.cpp` from that tag (`git show v<ver>:src/taskbar-autohide-any-edge.wh.cpp > /tmp/...`).
- [ ] **Step 4: Final check:**
  - `scan.py` on a fresh clone exits 0.
  - `gh release list` shows the three releases.
  - `.venv/bin/pytest -q` passes.
  - The README "Requirements" table matches `kKnownBuilds`.

  Tell the owner the repo is ready to make public with `gh repo edit --visibility public`, and that this step is theirs.
