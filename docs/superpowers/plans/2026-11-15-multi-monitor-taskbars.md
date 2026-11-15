# Taskbars on Every Display Implementation Plan

> **Status, end of 2026-11-15:** Tasks 0 to 16 are done, several by another
> route than written here; the design spec's status note lists the departures.
> Of Task 17: the harness replaces the hand-run layout matrix (stacked displays
> at equal and different scaling pass on all four edges; side-by-side and
> single-display layouts are still to run), the README, roadmap and mod header
> are updated, and the pre-release, the issue replies and the version bump to
> 2.0.0 wait for the owner.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix issues #9, #10 and #12, then hold every hidden taskbar on its own display on every edge (#11), with the mod running the hide and reveal itself.

**Architecture:** Decisions move into a block of pure functions inside the mod, compiled and tested on their own with Windhawk's compiler. The Windows-facing code keeps one record per taskbar window and asks those functions what to do. One pacing thread drives every animation; each step runs on the taskbar's thread.

**Tech Stack:** C++23 in one Windhawk mod file (`src/taskbar-autohide-any-edge.wh.cpp`), Windhawk 1.7.3's clang, pytest, PowerShell.

**Spec:** `docs/superpowers/specs/2026-11-15-multi-monitor-taskbars-design.md`. Read it first, with `research/spike-secondary-park.wh.cpp`, which is the working reference for Tasks 7 to 10.

**Scope:** all four of the spec's delivery steps, plus Task 16, a check of the whole mod against taskbar.dll that the owner added. Step 1 (`slidewindow-10066`) is already written and is Task 0. Tasks 1 to 5 are the small fixes, Tasks 6 to 11 the bottom edge, and Tasks 12 to 15 the left, top and right edges (the owner's decision: one release, tested on every edge before it ships). Only the bottom edge was spiked. Task 12 is therefore a measurement with a stop-and-report gate, and Tasks 13 to 15 are written against its expected result.

## Global Constraints

- Never handle, swallow or alter `WM_DPICHANGED`. Logging it is allowed.
- One display with the taskbar at the bottom behaves exactly as before this plan: no hook changes its result, no window is subclassed that was not before.
- A held taskbar's window travels at most `thickness / 2 - 2` pixels past its display's edge.
- "Animation frame rate": range 30 to 240, default 90. A taskbar steps at `min(display refresh, setting)`.
- Dwell on a held edge: `max(revealDwellMs, 150)` ms.
- `hideDelayMs`: range 0 to 10000, default 0. `notificationBurstGapMs`: default 10000.
- New symbol hooks are set from symbols only, are optional, and never join `kKnownBuilds`. Without them the mod logs one line and everything else works.
- Every setting needs its header block entry (description ending "Default: X."), its `Settings` field, its clamp in `LoadSettings`, and its README table row.
- "Skip edges facing another display" defaults to off from Task 15. While it is on, the mod stands down on left, top and right edges that face a display, as it does today.
- All commits signed. Never disable signing. No pushes without the owner's word. Work on `multimonitor-experiment`.
- A release with code changes uses `tools/release.sh` by hand, only when the owner asks.
- This work is released as **v2.0.0** (the owner's decision, 2026-11-15). Set `// @version` to 2.0.0 in the release step, not before: the watcher's build PRs still bump the minor version on main until then.

## Review Focus

Conditions the spec implies and no task's unit tests cover. Each is pinned to a task below.

1. **A display is unplugged, or the layout changes, while a taskbar is held and hidden.** It must not stay clipped or off-screen. (Task 10, step 4)
2. **A secondary taskbar appears after the mod loaded** ("show on all displays" turned on later, or a display plugged in). It must be handled without reloading the mod. (Task 7, test `test_records_follow_windows` and step 5)
3. **The mod is disabled mid-animation.** Regions are removed and every window ends where Windows last asked. (Task 10, step 3)
4. **A build with no published symbols.** The secondary unhide hook is skipped with one log line; nothing else changes. (Task 11, step 5)
5. **Start, Search or Quick Settings opened from a held taskbar.** The flyout is not clipped by the taskbar's region and the taskbar stays up. (Task 9, step 5)

---

### Task 0: Land `slidewindow-10066` and bring it into this branch

**Files:** none new. The work sits uncommitted in `/home/merson/windhawk-taskbar-autohide` on branch `slidewindow-10066`.

- [ ] **Step 1:** Ask the owner for the go-ahead to commit and open the PR. Do not commit that branch without it.
- [ ] **Step 2:** After it merges and the watcher has released the 10066 build, run `git merge origin/main` in this worktree.
- [ ] **Step 3:** Run `.venv/bin/pytest tests -q` and `tools/compile-mod.sh`. Expected: all pass, mod compiles.

### Task 1: A tested block of pure logic

**Files:**
- Modify: `src/taskbar-autohide-any-edge.wh.cpp` (new block after `LoadSettings`)
- Create: `tests/test_mod_logic.py`
- Modify: `.github/workflows/ci.yml` (compile job runs `tests/test_mod_guard.py tests/test_mod_logic.py`)

**Interfaces:**
- Produces: the markers `// ==PureLogic==` and `// ==/PureLogic==` in the mod. Code between them may use `<windows.h>` types only, and no globals. In the test file: `build_logic(tmp_path) -> run`, where `run(command: str, *args: int) -> list[int]` prints nothing but integers; and the fixture `logic`. The harness `main` is a chain of `if (!strcmp(argv[1], "<command>"))` branches, one per function, added by the task that adds the function. `tests/test_mod_guard.py::build` is reused for compiling.

- [ ] **Step 1: Write the failing test**

```python
def test_block_is_found_and_compiles(logic):
    assert logic('answer') == [42]   # a branch that prints 42 and uses nothing
```

- [ ] **Step 2:** Run `.venv/bin/pytest tests/test_mod_logic.py -q`. Expected: FAIL, "PureLogic block not found in the mod".
- [ ] **Step 3:** Add the empty marked block to the mod and write `build_logic`, extracting everything between the markers with one regex and failing with that message when absent.
- [ ] **Step 4:** Run the test and `tools/compile-mod.sh`. Expected: PASS, mod compiles.
- [ ] **Step 5:** Commit: `test: a block of the mod's pure logic, compiled and run on its own`.

### Task 2: Notification burst rule (#9, primary)

**Files:**
- Modify: `src/taskbar-autohide-any-edge.wh.cpp` (PureLogic block; `TrayUI_Unhide_Hook`; `Settings` default; header block entry `notificationBurstGapMs`), `README.md:81`
- Test: `tests/test_mod_logic.py`

**Interfaces:**
- Produces:

```cpp
struct BurstState { LONG64 lastUnhide; LONG64 holdUntil; int request; };
enum BurstVerdict { BURST_BEGIN = 0, BURST_PASS = 1, BURST_DROP = 2 };
// Updates *s. BEGIN: a new burst, show for holdMs. PASS: let Windows show it.
// DROP: a repeat of the burst's own request after its hold; keep hidden.
BurstVerdict BurstDecide(BurstState* s, LONG64 now, int request, int holdMs,
                         int gapMs, bool pointerAtEdge, bool surfaceInFront);
```

Harness command `burst holdMs gapMs (now request pointerAtEdge surfaceInFront)...` prints one verdict per event, carrying one `BurstState` across them.

- [ ] **Step 1: Write the failing tests**

```python
HOLD, GAP = 1500, 10000

def burst(logic, *events):
    return logic('burst', HOLD, GAP, *[v for e in events for v in e])

def test_repeat_flashes_show_once(logic):
    # request 17 every 5.6 s: shown once, then dropped
    assert burst(logic, (0, 17, 0, 0), (5600, 17, 0, 0), (11200, 17, 0, 0)) == [0, 2, 2]

def test_stray_request_passes_without_starting_a_burst(logic):
    # a 15 between the 17s is shown, and the next 17 is still a repeat
    assert burst(logic, (0, 17, 0, 0), (3000, 15, 0, 0), (5600, 17, 0, 0)) == [0, 1, 2]

def test_repeat_inside_its_hold_passes(logic):
    assert burst(logic, (0, 17, 0, 0), (1000, 17, 0, 0)) == [0, 1]

def test_quiet_longer_than_the_gap_begins_again(logic):
    assert burst(logic, (0, 17, 0, 0), (10001, 17, 0, 0)) == [0, 0]

def test_pointer_or_open_surface_lets_a_repeat_through(logic):
    assert burst(logic, (0, 17, 0, 0), (5600, 17, 1, 0)) == [0, 1]
    assert burst(logic, (0, 17, 0, 0), (5600, 17, 0, 1)) == [0, 1]
```

- [ ] **Step 2:** Run them. Expected: FAIL, unknown command `burst`.
- [ ] **Step 3:** Implement `BurstDecide`. `lastUnhide` becomes `now` on every call. A different request inside a burst returns `BURST_PASS` and leaves `request` and `holdUntil` alone.
- [ ] **Step 4:** Rewrite the burst section of `TrayUI_Unhide_Hook` to call it with one global `BurstState`, passing `ShellSurfaceInForeground()` (not `ShellHasFocus()`) and keeping the existing `edge <= ABE_BOTTOM` and `!g_inWatcherCall` conditions. `g_holdVisibleUntil` stays the value other code reads: copy `holdUntil` into it on `BURST_BEGIN`. Change the gap's default to 10000 in the struct, the header block and the README.
- [ ] **Step 5:** Run `.venv/bin/pytest tests/test_mod_logic.py -q` and `tools/compile-mod.sh`. Expected: PASS.
- [ ] **Step 6:** Commit: `fix: a stray unhide request no longer restarts the notification burst (#9)`.

### Task 3: Quick Settings keeps the taskbar up (#10)

**Files:**
- Modify: `src/taskbar-autohide-any-edge.wh.cpp` (`kShellHosts`; PureLogic block; new `QuickSettingsOpen`; `WatcherTick` hold condition; `CTaskListWnd_PermitAutoHide_Hook`), `tools/measure-taskbars.ps1` (new `-Class` parameter)
- Test: `tests/test_mod_logic.py`

**Interfaces:**
- Produces: `bool PanelCountsAsOpen(bool visible, bool cloaked, bool onDisplay)` (PureLogic; true only for visible, not cloaked, on a display) and `bool QuickSettingsOpen()` (loops `FindWindowExW(nullptr, w, L"ControlCenterWindow", nullptr)`, with `IsWindowVisible`, `DwmGetWindowAttribute(DWMWA_CLOAKED)` and `MonitorFromWindow(..., MONITOR_DEFAULTTONULL)`). The mod gains `-ldwmapi` in `@compilerOptions`.

- [ ] **Step 1: Find out how a dismissed panel looks.** Add `-Class <name>` to `tools/measure-taskbars.ps1`: it lists every top-level window of that class with rectangle, visible, cloaked and display, in the snapshot and in `-Watch`. With the owner: run `-Class ControlCenterWindow -Watch 30`, open Quick Settings, dismiss it with Esc, with a click away and with Win+A. Record the three states in the spec under "What was measured". If a dismissed panel is still visible, not cloaked and on a display, stop and report: the rule in the spec does not work.
- [ ] **Step 2: Write the failing test**

```python
def test_panel_open_only_when_visible_uncloaked_and_on_a_display(logic):
    cases = [(v, c, d) for v in (0, 1) for c in (0, 1) for d in (0, 1)]
    assert [logic('panel', *case)[0] for case in cases] == [0, 0, 0, 0, 0, 1, 0, 0]
```

- [ ] **Step 3:** Run it. Expected: FAIL, unknown command `panel`.
- [ ] **Step 4:** Implement both functions. Add `L"ShellHost.exe"` to `kShellHosts`. In `WatcherTick`, add `|| QuickSettingsOpen()` as the last term of the condition that resets `g_leaveTicks`. In `CTaskListWnd_PermitAutoHide_Hook`, return the original's answer, not `TRUE`, while `QuickSettingsOpen()`.
- [ ] **Step 5:** Run the tests and `tools/compile-mod.sh`. Expected: PASS.
- [ ] **Step 6: Manual check with the owner.** Off the bottom edge and on it: open Quick Settings, move focus to another window, the taskbar stays up; dismiss the panel each of the three ways, the taskbar hides within two seconds.
- [ ] **Step 7:** Commit: `fix: the taskbar stays up while Quick Settings is open (#10)`.

### Task 4: Hide delay (#12)

**Files:**
- Modify: `src/taskbar-autohide-any-edge.wh.cpp` (header block, `Settings`, `LoadSettings`, PureLogic block, `WatcherTick`), `README.md` settings table
- Test: `tests/test_mod_logic.py`

**Interfaces:**
- Produces: setting `hideDelayMs` ("Hide delay (ms)"); `bool HideDelayElapsed(LONG64 now, LONG64 lastKeptUp, int delayMs)`; global `LONG64 g_lastKeptUp` (taskbar thread only).

- [ ] **Step 1: Write the failing test**

```python
def test_hide_delay(logic):
    assert logic('delay', 1000, 1000, 0) == [1]      # 0 hides at once
    assert logic('delay', 1499, 1000, 500) == [0]
    assert logic('delay', 1500, 1000, 500) == [1]
```

- [ ] **Step 2:** Run it. Expected: FAIL, unknown command `delay`.
- [ ] **Step 3:** Implement. In `WatcherTick`, set `g_lastKeptUp` to now in both early returns that reset `g_leaveTicks` (pointer or surface; notification hold) and on the mod's reveal path, and return before the `LEAVE_TICKS` test while the delay has not elapsed. Clamp 0 to 10000. The description says it has no effect on a bottom edge that Windows hides itself, and that it runs after the notification hold.
- [ ] **Step 4:** Run the tests and `tools/compile-mod.sh`. Expected: PASS.
- [ ] **Step 5:** Commit: `feat: hide delay setting (#12)`.

### Task 5: Checkpoint for the small fixes

- [ ] **Step 1:** Run `.venv/bin/pytest tests -q -rs`. Expected: all pass.
- [ ] **Step 2: Manual check with the owner**, taskbar off the bottom edge: `tools/flash-taskbar.ps1` with a long count shows the taskbar once; Win key still reveals it mid-flash; hide delay of 1000 keeps it up for a second after the pointer leaves.
- [ ] **Step 3:** Report to the owner that delivery step 2 is releasable. Release only if asked.

### Task 6: Geometry

**Files:**
- Modify: `src/taskbar-autohide-any-edge.wh.cpp` (PureLogic block)
- Test: `tests/test_mod_logic.py`

**Interfaces:**
- Produces (edges are `ABE_LEFT 0, ABE_TOP 1, ABE_RIGHT 2, ABE_BOTTOM 3`):

```cpp
// Another display touches the one-pixel band just past `edge` of `own`.
bool DisplayBeyond(const RECT& own, UINT edge, const RECT* others, int count);

struct BarPlacement { int x, y; RECT region; bool clipped; };
// Where a taskbar's window and region go with hiddenPx of its thickness
// hidden. shown: its top-left when fully shown. Not held: the window travels
// hiddenPx and is not clipped unless hiddenPx > 0 (region = the part still on
// its display). Held: it travels min(hiddenPx, thickness/2 - 2).
BarPlacement PlaceBar(POINT shown, SIZE size, UINT edge, bool held, int hiddenPx);

RECT RevealStrip(const RECT& display, const RECT& bar, UINT edge, int widthPx, UINT dpi);
int HeldDwellMs(int settingMs);                 // max(settingMs, 150)
int StepsPerSecond(int displayHz, int capFps);  // min of the two; displayHz <= 1 counts as 60
```

Harness commands `beyond`, `place`, `strip`, `dwell`, `fps`, each printing the result's fields as integers in declaration order.

- [ ] **Step 1: Write the failing tests.** Use the owner's measured layout as the first case of each.

```python
PRIMARY, UPPER = (0, 0, 3440, 1440), (988, -2560, 2428, 0)

def test_beyond(logic):
    b = lambda own, edge, *others: logic('beyond', *own, edge, *[v for o in others for v in o])[0]
    assert b(UPPER, 3, PRIMARY) == 1 and b(PRIMARY, 3, UPPER) == 0   # stacked
    assert b(PRIMARY, 1, UPPER) == 1                                  # top of the lower one
    assert b((0, 0, 1920, 1080), 2, (1920, 0, 3840, 1080)) == 1       # side by side
    assert b((0, 0, 1920, 1080), 3, (1920, 0, 3840, 1080)) == 0
    assert b((0, 0, 1920, 1080), 2, (1920, 1000, 3840, 2080)) == 1    # partly facing counts
    assert b((0, 0, 1920, 1080), 2, (1920, 1080, 3840, 2160)) == 0    # corners only
    assert b((-1920, 0, 0, 1080), 0) == 0                             # negative coordinates, alone
    assert b((0, 0, 1920, 1080), 3, (1920, 0, 3840, 1080), (0, 1080, 1920, 2160)) == 1

def test_place_held_bottom_matches_the_spike(logic):
    place = lambda hidden: logic('place', 988, -60, 1440, 60, 3, 1, hidden)
    assert place(0) == [988, -60, 0, 0, 0, 0, 0]                # shown, no region
    assert place(10) == [988, -50, 0, 0, 1440, 50, 1]           # sliding, overhang clipped
    assert place(58) == [988, -32, 0, 30, 1440, 32, 1]          # travel 28, 2 px left

def test_place_not_held_slides_all_the_way(logic):
    assert logic('place', 0, 1392, 3440, 48, 3, 0, 46) == [0, 1438, 0, 0, 3440, 2, 1]

def test_held_window_is_never_mostly_off_its_display(logic):
    for edge, shown in ((0, (0, 0)), (1, (0, 0)), (2, (1860, 0)), (3, (0, 1020))):
        size = (60, 1080) if edge in (0, 2) else (1920, 60)
        for hidden in range(0, 61):
            x, y, *_ = logic('place', *shown, *size, edge, 1, hidden)
            assert abs(x - shown[0]) + abs(y - shown[1]) <= 28

def test_strip_scales_with_dpi(logic):
    assert logic('strip', *UPPER, 988, -60, 2428, 0, 3, 8, 120) == [988, -10, 2428, 0]
    assert logic('strip', *PRIMARY, 0, 1392, 3440, 1440, 3, 8, 96) == [0, 1432, 3440, 1440]

def test_dwell_and_rate(logic):
    assert [logic('dwell', v)[0] for v in (0, 150, 400)] == [150, 150, 400]
    assert [logic('fps', *v)[0] for v in ((60, 90), (240, 90), (240, 240), (0, 90))] == [60, 90, 240, 60]
```

- [ ] **Step 2:** Run them. Expected: FAIL, unknown commands.
- [ ] **Step 3:** Implement the five functions. `PlaceBar`'s region arithmetic is the spike's `ApplyOffset`.
- [ ] **Step 4:** Run the tests. Expected: PASS.
- [ ] **Step 5:** Commit: `feat: geometry for taskbars held on their own display`.

### Task 7: One record per taskbar

**Files:**
- Modify: `src/taskbar-autohide-any-edge.wh.cpp`
- Test: `tests/test_mod_logic.py`

**Interfaces:**
- Consumes: `DisplayBeyond`.
- Produces:

```cpp
struct TaskbarRec {
    HWND hwnd; bool primary;
    RECT display; int displayHz; UINT edge; bool held;
    POINT shown; int hiddenAsked; int hiddenNow;   // pixels of thickness
    // animation and reveal fields are added by Tasks 8 and 9
};
TaskbarRec* RecFor(HWND hwnd);     // taskbar thread only
bool AnyHeld();
// Pure: which of `have` to drop and which of `seen` to add.
int DiffWindows(const HWND* have, int nHave, const HWND* seen, int nSeen,
                HWND* toAdd, HWND* toDrop, int* nDrop);   // returns nAdd
```

Harness command `diff nHave have... nSeen seen...` prints `nAdd adds... nDrop drops...`.

Records are refreshed on the taskbar thread from the existing watch tick, at most once a second, and on `WM_DISPLAYCHANGE` and `WM_SETTINGCHANGE` seen by the primary's subclass. Secondary windows are subclassed with a new `SecondaryProc`; the primary keeps `TrayWatchProc`.

- [ ] **Step 1: Write the failing test**

```python
def test_records_follow_windows(logic):
    # have {1,2}, seen {2,3}: add 3, drop 1
    assert logic('diff', 2, 1, 2, 2, 2, 3) == [1, 3, 1, 1]
    assert logic('diff', 0, 2, 5, 6) == [2, 5, 6, 0]     # both new
    assert logic('diff', 2, 5, 6, 0) == [0, 2, 5, 6]     # both gone
```

- [ ] **Step 2:** Run it. Expected: FAIL, unknown command `diff`.
- [ ] **Step 3:** Implement `DiffWindows`, the record table (8 entries), the refresh, and `SecondaryProc` (for now it only drops the record on `WM_NCDESTROY`). `held` is always false when the docked edge is not the bottom, until Task 15.
- [ ] **Step 4:** Log one line per record when it is made or its facts change: class, rectangle, display, Hz, edge, held.
- [ ] **Step 5: Manual check with the owner.** Stacked layout: the log shows two records, the upper one held. Turn "show my taskbar on all displays" off and on: the secondary's record goes and comes back without reloading the mod. One display, taskbar at the bottom: one record, not held, and `tools/measure-taskbars.ps1 -Watch 20` shows the same rectangles as with the mod disabled.
- [ ] **Step 6:** Commit: `feat: one record per taskbar window`.

### Task 8: The mod runs the animation on the bottom edge

**Files:**
- Modify: `src/taskbar-autohide-any-edge.wh.cpp`

**Interfaces:**
- Consumes: `TaskbarRec`, `RecFor`, `AnyHeld`, `PlaceBar`, `StepsPerSecond`.
- Produces: `void StartBarAnimation(TaskbarRec* rec, int hiddenTarget)`; a pacing thread started in `Wh_ModInit` and joined in `Wh_ModUninit`; registered message `g_barTickMsg` handled by both subclass procs, returning 1 while its taskbar is still animating.

Behaviour, all taken from the spike (versions 0.3 to 0.5):

- The mod **handles** a taskbar when the docked edge is the bottom and `AnyHeld()`. Otherwise nothing in this task runs. Tasks 13 to 15 widen this to the other edges.
- `TrayUI_SlideWindow_Hook`: for a handled window, call the original with `animate = false`. Existing behaviour for other windows is unchanged.
- `WM_WINDOWPOSCHANGING` on a handled window, not the mod's own move: work out how many pixels Windows wants hidden; if that differs from `hiddenAsked`, call `StartBarAnimation`; then replace `x`, `y` with `PlaceBar(...)` for `hiddenNow`.
- The mod's own moves: remember the requested `x`, `y` before `DefSubclassProc` and put them back after it. The primary's handler replaces them otherwise.
- Each tick: eased position `1 - (1 - t)^3`, duration `hideDurationMs` or 400 for a hide and `showDurationMs` or 200 for a reveal, then `SetWindowPos` and `SetWindowRgn` from `PlaceBar`.
- Pacing: a `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` timer, aiming at frame boundaries from the animation's start; frame = `1000000 / StepsPerSecond(displayHz, frameRate)` microseconds, the shortest among animating taskbars.

- [ ] **Step 1:** Implement the above. Keep the spike's per-animation log line (steps, shortest and longest gap, late steps, DPI) behind the mod's existing logging.
- [ ] **Step 2:** Run `.venv/bin/pytest tests -q` and `tools/compile-mod.sh`. Expected: PASS.
- [ ] **Step 3: Manual check with the owner**, stacked layout, bottom edge. Run `tools/measure-taskbars.ps1 -Watch 30` while hiding and revealing each taskbar five times. Expected: the upper taskbar's DPI is 120 on every line; its rectangle never has more pixels on the primary than on its own display; the log shows no late steps beyond one in ten animations; the primary slides fully and the upper one half-slides.
- [ ] **Step 4:** One display, bottom edge: the log shows no animation lines at all.
- [ ] **Step 5:** Commit: `feat: held taskbars stay on their own display on the bottom edge (#11)`.

### Task 9: Revealing a held taskbar

**Files:**
- Modify: `src/taskbar-autohide-any-edge.wh.cpp`

**Interfaces:**
- Consumes: `RevealStrip`, `HeldDwellMs`, `TaskbarRec`.
- Produces: `void RevealHeld(TaskbarRec* rec, POINT pointer)`.

- [ ] **Step 1: Separate the two messages.** In a scratch copy of the spike, send only `WM_NCHITTEST`, then only `WM_MOUSEMOVE`, and note which reveals the taskbar. Record the answer in the spec. Use only what is needed.
- [ ] **Step 2:** In the watch tick, for each held record with `hiddenAsked > 0`: when the pointer has been inside `RevealStrip(display, bar, edge, revealTolerance, dpi)` for `HeldDwellMs(revealDwellMs)`, call `RevealHeld`, which sends the message at a point inside the taskbar's current region, level with the pointer. Send once per dwell; if the taskbar is still hidden a second later, log it and allow another attempt.
- [ ] **Step 3:** Run `tools/compile-mod.sh`. Expected: compiles.
- [ ] **Step 4: Manual check with the owner.** Resting in the strip reveals the upper taskbar. Moving straight through to the primary does not. The primary's top row no longer reveals it.
- [ ] **Step 5: Manual check, Review Focus 5.** From the held taskbar open Start, Search and Quick Settings: each opens whole, on the upper display, and the taskbar stays up until it closes.
- [ ] **Step 6:** Commit: `feat: reveal a held taskbar from its own display`.

### Task 10: Leaving things as Windows had them

**Files:**
- Modify: `src/taskbar-autohide-any-edge.wh.cpp`

**Interfaces:**
- Produces: `void ReleaseBar(TaskbarRec* rec)`: stops its animation, removes its region, and moves the window to the position Windows last asked for.

- [ ] **Step 1:** Call `ReleaseBar` when a record stops being handled (the layout changed so nothing is held, the edge is no longer the bottom, auto-hide turned off), when its display facts change, and for every record in `Wh_ModUninit` after the pacing thread has stopped.
- [ ] **Step 2:** Run `tools/compile-mod.sh`. Expected: compiles.
- [ ] **Step 3: Manual check, Review Focus 3.** Disable the mod while a taskbar is mid-animation, ten times. `tools/measure-taskbars.ps1` then shows Windows' own hidden rectangle `(988,-2,2428,58)` or the shown one, and the taskbar is drawn whole when revealed.
- [ ] **Step 4: Manual check, Review Focus 1.** With the upper taskbar held and hidden, move the upper display beside the primary in Display settings, then unplug it. Each time, every remaining taskbar can be revealed and is drawn whole.
- [ ] **Step 5:** Commit: `fix: release held taskbars when the layout changes or the mod unloads`.

### Task 11: Flashing buttons on secondary taskbars (#9)

**Files:**
- Modify: `src/taskbar-autohide-any-edge.wh.cpp`, `research/symbol-map.tsv`

**Interfaces:**
- Consumes: `BurstDecide`, `BurstState`, `TaskbarRec`.
- Produces: `BurstState burst` inside `TaskbarRec`; hook `CSecondaryTray_Unhide_Hook`.

- [ ] **Step 1: Find the function.** List the public symbols of a published build's `Taskbar.pdb` whose names contain `CSecondaryTray` and `Unhide` (the PDBs are in `~/.cache/taskbar-matcher`; `tools/matcher/pdb.py` reads publics). Record the exact undecorated strings of `Unhide` and `_Unhide`, and from the disassembly whether `Unhide` calls `_Unhide` and how a `CSecondaryTray*` leads to its window. Add them to `research/symbol-map.tsv`.
- [ ] **Step 2:** Move the primary's `BurstState` into its record. `BurstDecide` is already tested in Task 2; what is new here is Windows-facing and is checked by hand in the last step. Hook `_Unhide` only, in a second `HookSymbols` call whose failure logs `secondary taskbars: notification handling unavailable on this build` and is otherwise ignored. The hook applies `BurstDecide` with that taskbar's record and its own hold; `CurrentStuckPlace` must not be called on a `CSecondaryTray`.
- [ ] **Step 3:** Make `CTaskListWnd_PermitAutoHide_Hook` use the hold of the taskbar the task list belongs to, not the primary's.
- [ ] **Step 4:** Run the tests and `tools/compile-mod.sh`. Expected: PASS.
- [ ] **Step 5: Manual check with the owner.** Two displays, a button flashing with a long count: each taskbar shows once. **Review Focus 4:** rename Windhawk's cached symbols for the running build so the lookup fails, reload: the one log line appears, the primary behaves as before, Explorer does not crash. Restore the cache.
- [ ] **Step 6:** Commit: `fix: a flashing button shows each secondary taskbar once (#9)`.

### Task 12: Measure the left, top and right edges

**Files:**
- Modify: `docs/superpowers/specs/2026-11-15-multi-monitor-taskbars-design.md` ("What was measured")

Nothing below the bottom edge has been tried with more than one taskbar. `docs/ROADMAP.md` section 3 lists what is unknown.

- [ ] **Step 1: With the owner, mod disabled, auto-hide and "show on all displays" on.** For the taskbar on the left, then the top, then the right, in a layout where the secondary's docked edge faces the primary and one where it does not, run `tools/measure-taskbars.ps1 -Watch 30`. Record for each: does the secondary hide at all; where it parks; its DPI when parked; whether Windows reveals it.
- [ ] **Step 2: Same with the released mod enabled** and "Skip edges facing another display" off. Record what the primary and the secondary each do.
- [ ] **Step 3: Read `CSecondaryTray`.** From a published build's PDB and disassembly, record in `research/symbol-map.tsv` whether it has its own `GetAutoHideFlags`, `SetAutoHideFlags`, `GetStuckPlace` and `GetDockedRect`, their signatures, and whether its `GetAutoHideFlags` masks the flags off the bottom edge as `TrayUI`'s does.
- [ ] **Step 4: Write the results into the spec and report to the owner.** Then take one branch:
  - **A. The secondary hides on these edges without help.** Go on to Task 13; Task 14 only holds it.
  - **B. It does not hide.** Go on to Task 13; Task 14 makes the mod hide it by its window.
  - **C. Something else** (it hides but cannot be revealed, Explorer misbehaves, the layout is not stable). Stop and bring it to the owner before Task 13.
- [ ] **Step 5:** Commit: `docs: how secondary taskbars behave off the bottom edge`.

### Task 13: The primary's off-bottom hide and reveal go through the same animation

**Files:**
- Modify: `src/taskbar-autohide-any-edge.wh.cpp`
- Test: `tests/test_mod_logic.py`

**Interfaces:**
- Consumes: `TaskbarRec`, `StartBarAnimation`, `PlaceBar`, `g_barTickMsg`.
- Produces: `int HiddenTarget(int thickness, bool hideFully, bool held)` (PureLogic): `thickness` when `hideFully` and not held, else `thickness - 2`. Removes: `Sleep_Hook`, `GetTickCount_Hook`, the frame timer that served them, `ApplyDurationOverrides`, `g_durationOffsetsVerified`, and the watch thread's own slide (`g_animRequested`, `g_animFrom`, `g_animTo`, `g_animOverride`).

Today, off the bottom edge, the watch thread slides the primary out itself and Windows' own loop slides it back in, re-paced by the `Sleep` and `GetTickCount` hooks. After this task both directions are `StartBarAnimation` on the primary's record, on every edge the mod handles.

- [ ] **Step 1: Write the failing test**

```python
def test_hidden_target(logic):
    assert logic('target', 48, 1, 0) == [48]   # hide completely, nothing beyond
    assert logic('target', 48, 0, 0) == [46]   # leave Windows' 2 px sliver
    assert logic('target', 60, 1, 1) == [58]   # held: the sliver stays for the reveal
```

- [ ] **Step 2:** Run it. Expected: FAIL, unknown command `target`.
- [ ] **Step 3:** Implement `HiddenTarget`. In `WatcherTick`, replace the hide slide request with `StartBarAnimation(rec, HiddenTarget(...))`; keep everything before it (the flag changes, `g_inWatcherCall`). In `TrayUI_SlideWindow_Hook`, every window the mod handles gets `animate = false`. The off-screen pin (`g_clipHold`, `g_clipRect`) becomes the record's `hiddenNow`: the `WM_WINDOWPOSCHANGING` replacement from Task 8 already holds the window there.
- [ ] **Step 4:** Delete the hooks and globals listed above and their set-up in `SetUpHooks`. `ScanForDurationLoads` stays in the mod only if something still calls it; `tools/matcher/scan.py` keeps its port, which the matcher's anchor uses, and `tools/matcher/buildcheck.py`'s `durations` result stops being a reason the mod "will not work": update `tools/watch_build.py:check_published`, `tools/release_notes.py` and their tests to match.
- [ ] **Step 5:** Run `.venv/bin/pytest tests -q -rs` and `tools/compile-mod.sh`. Expected: PASS.
- [ ] **Step 6: Manual check with the owner, one display's worth of behaviour first.** Primary on the left, top and right with no display beyond that edge: hide, reveal by pointer, reveal by Win key, a flashing button, Start and Quick Settings, "Hide completely" on and off, both duration settings, frame rate 30, 90 and 240. Each behaves as the released mod does, and `-Watch` shows the same end rectangles.
- [ ] **Step 7:** Commit: `refactor: one animation for every edge, without the Sleep and GetTickCount hooks`.

### Task 14: Secondary taskbars on the left, top and right edges

**Files:**
- Modify: `src/taskbar-autohide-any-edge.wh.cpp`

**Interfaces:**
- Consumes: `TaskbarRec`, `StartBarAnimation`, `HiddenTarget`, `RevealStrip`, `HideDelayElapsed`, Task 12's results.
- Produces: `void WatchSecondary(TaskbarRec* rec, POINT pointer, LONG64 now)`, called from the watch tick for each secondary record off the bottom edge.

- [ ] **Step 1 (branch A, the secondary hides without help):** nothing to add here; Task 15 makes these records held.
- [ ] **Step 1 (branch B, it does not hide):** implement `WatchSecondary` as the per-record form of what `WatcherTick` does for the primary, without touching any `CSecondaryTray` member: the taskbar stays up while the pointer is over it or its strip, a shell surface is in front, Quick Settings is open, its notification hold runs, or the hide delay has not elapsed; otherwise `StartBarAnimation(rec, HiddenTarget(...))`. It is revealed by `StartBarAnimation(rec, 0)` when the pointer rests in its strip or Windows asks for it (`CSecondaryTray_Unhide_Hook` from Task 11, where symbols allow). Windows does not know this taskbar is hidden, so the `WM_WINDOWPOSCHANGING` replacement is what keeps it there.
- [ ] **Step 2:** Run `tools/compile-mod.sh`. Expected: compiles.
- [ ] **Step 3: Manual check with the owner**, taskbar on the left, top and right, secondary's edge not facing another display: the secondary hides, is revealed from its own edge, stays up under the pointer and with Start open, and `-Watch` shows its DPI never changing. Maximise a window on that display: note whether the space the taskbar occupied is given to the window, and bring the answer to the owner if it is not.
- [ ] **Step 4:** Commit: `feat: secondary taskbars auto-hide on the left, top and right edges`.

### Task 15: Held taskbars on every edge, and "Skip edges" off by default

**Files:**
- Modify: `src/taskbar-autohide-any-edge.wh.cpp` (`held` in the record refresh; `EdgeEnabled`; `DisplayBeyondEdge` and `EdgeFacesDisplay`, which the records replace; the header block and `Settings` default for `skipAdjacentEdges`), `README.md`

**Interfaces:**
- Consumes: `DisplayBeyond`, `PlaceBar`, `RevealStrip`, `HeldDwellMs`, `RevealHeld`.

- [ ] **Step 1:** Compute `held` from `DisplayBeyond` on every edge, not only the bottom. `EdgeEnabled(edge)` returns false for a left, top or right edge only when `skipAdjacentEdges` is on and the primary's record says a display lies beyond it. Remove `DisplayBeyondEdge`, `EdgeFacesDisplay` and their cache.
- [ ] **Step 2:** `skipAdjacentEdges` defaults to false in the struct and the header block. Its description says it is a way out: on, the mod leaves an edge that faces another display alone.
- [ ] **Step 3:** Run `.venv/bin/pytest tests -q -rs` and `tools/compile-mod.sh`. Expected: PASS. Task 6's `test_held_window_is_never_mostly_off_its_display` already covers the four edges.
- [ ] **Step 4: Manual check with the owner.** For the taskbar on the left, top and right, in a layout where the primary's edge faces the other display and one where the secondary's does: each held taskbar half-slides, keeps its DPI in `-Watch`, is revealed only from its own display, and does not appear when the pointer crosses to the other display without resting.
- [ ] **Step 5:** With `skipAdjacentEdges` turned on: the mod stands down on a facing edge exactly as the released mod does.
- [ ] **Step 6:** Commit: `feat: held taskbars on every edge; "Skip edges facing another display" off by default`.

### Task 16: Check the whole mod against taskbar.dll, then clean up

Added by the owner on 2026-11-15: "run the entire code pipeline through the same REA analysis, so all the things not from today. See if we can clean up code and optimise things."

Everything written before 2026-11-15 rests on what was learned by experiment and from one hand analysis of 9549/9807. Today several fixes were written from guesses and some were wrong; reading taskbar.dll settled them. This task does the same for the rest of the mod, then removes what the reading shows is unneeded.

**Files:**
- Read: `src/taskbar-autohide-any-edge.wh.cpp` (all of it), `research/symbol-map.tsv`, `docs/findings-phase1.md`, `docs/ROADMAP.md`
- Create: `docs/taskbar-internals.md` (what the code shows, with function names and RVAs for 9974 and 10066)
- Modify: the mod, its tests, `research/symbol-map.tsv`

**Method:** REA on taskbar.dll 10.0.26100.9974 (symbols published) and 10.0.26100.10066 (the build the owner will be on; names by matching against 9974). Long digs go to a subagent; only conclusions come back. For every claim: observed in code, or inferred, and from which function.

- [ ] **Step 1: List every assumption the mod makes about Windows.** Go through the source top to bottom and write each one down with its line: each hook and why it exists, each member offset (`ScanForOffsets`: edge and flags; the duration members), each message or flag value (`ABS_ALWAYSONTOP` as "hidden", unhide requests, `WM_SETTINGCHANGE` nudges, the `SWP_FRAMECHANGED` nudge), each timing constant (`WATCH_TICK_MS`, `LEAVE_TICKS`, the warning delays, the settle period), each window class and child it relies on, and each comment that states how Windows behaves.
- [ ] **Step 2: Check each against the code.** For each item: confirmed, wrong, or not determinable from taskbar.dll. Include at least:
  - `GetAutoHideFlags`, `SetAutoHideFlags`, `GetDockedRect`, `Unhide`, `SlideWindow`, `PermitAutoHide`: what each really does, every caller, and whether the mod's hook is still the smallest correct intervention now that it animates and positions the taskbars itself.
  - The watcher: whether TrayUI's own hide path (`_Hide`, `_SetAutoHideTimer`, `_OnTimer`) can be made to run off the bottom edge instead of the mod polling every 40 ms, and what stops it today (the earlier dig saw an edge test under a feature flag at the start of `_Hide`).
  - `RaiseTaskbarIfDemoted` and "Keep the taskbar above other windows": what demotes the taskbar and whether Windows has a call for restoring it.
  - `PointerOverShellSurface`, `ShellSurfaceInForeground`, the shell host list, `QuickSettingsOpen`: what Windows itself checks before hiding (the secondary's `_OnTimer` conditions are known; find the primary's) and whether the mod can ask Windows instead of keeping its own list.
  - The Sleep and GetTickCount hooks: still needed only where Windows' own slide runs (the bottom edge with nothing held); confirm, and see whether calling `SlideWindow` unanimated plus the mod's own animation there too would let them go without changing one-display bottom behaviour that the owner cares about.
  - `ScanForDurationLoads`: no longer used by the mod; decide with the matcher's needs in mind.
  - Unload: `RestoreTaskbarIfHidden` writing TrayUI's flags directly, against what `SetAutoHideState` does.
  - TrayUI+0x68 (the saved setting) for the first minute after Explorer starts.
  - Everything in today's ledger marked "not checked against code".
- [ ] **Step 3: Write `docs/taskbar-internals.md`.** One section per subsystem (edge change, auto-hide state, hide and unhide, window position, secondary taskbars, notifications, cloaking, content window), each stating what the code does, the functions and RVAs, and what remains inferred. It replaces the scattered comments as the reference, and `docs/findings-phase1.md` and the ROADMAP point to it.
- [ ] **Step 4: Bring the list to the owner** as a table: assumption, verdict, proposed change (remove, simplify, fix, keep), risk. No code changes before this.
- [ ] **Step 5: Apply the agreed changes, one commit each,** tests first where a pure function is involved. Delete code the reading shows is dead or redundant, correct comments that state Windows' behaviour wrongly, and replace polling or guessing with Windows' own calls where the code offers one.
- [ ] **Step 6:** Run `.venv/bin/pytest tests -q -rs` and `tools/compile-mod.sh`. Expected: PASS. Report the mod's size before and after and which hooks remain.
- [ ] **Step 7: Owner re-check** of each edge after the clean-up, with a recording, compared frame by frame against the last accepted recording.

### Task 17: The layout matrix, documents and the pre-release

**Files:**
- Modify: `tools/measure-taskbars.ps1`, `README.md`, the mod's header block and readme, `docs/ROADMAP.md`

- [ ] **Step 1:** Add `-Assert` to `tools/measure-taskbars.ps1`: with `-Watch`, exit 1 and name the taskbar if any taskbar's DPI changes, or if a taskbar that is not fully shown has more pixels on another display than on its own.
- [ ] **Step 2: Layout matrix with the owner.** Four layouts (stacked with the 125% display above; stacked with it below; side by side; stacked and offset so the edges only partly face) times four edges. For each of the sixteen, run `-Watch 30 -Assert` while hiding and revealing every taskbar. Expected: exit 0 each time. Record the table in the spec; any failure goes to the owner before the release.
- [ ] **Step 3:** One display, taskbar at the bottom: `-Watch 20` gives the same rectangles as with the mod disabled, and the log has no animation lines.
- [ ] **Step 4:** README: the settings table (hide delay; frame rate as a cap; the gap's new default; "Skip edges" off by default), the notification section, the note on the animation durations, and "Primary taskbar only" replaced by what now holds. ROADMAP sections 2b and 3: say what this supersedes.
- [ ] **Step 5:** Run `.venv/bin/pytest tests -q -rs` and `tools/compile-mod.sh`. Expected: PASS.
- [ ] **Step 6:** Commit: `docs: taskbars on every display`.
- [ ] **Step 7:** Tell the owner the branch is ready for a pre-release build to go to the reporter of #11, and wait. Drafting replies on the four issues, opening a PR and releasing all need the owner's word.
