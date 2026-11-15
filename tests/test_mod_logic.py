"""The mod's pure logic: the block between // ==PureLogic== and
// ==/PureLogic==, compiled on its own with Windhawk's compiler and driven
through a small command line. Each command takes integers and prints
integers, so a test reads as inputs and expected outputs."""
import re
import subprocess

import pytest

from test_mod_guard import SRC, compile_harness

HARNESS = r'''
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
%s

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const char* cmd = argv[1];
    long long a[64] = {};
    int n = 0;
    for (int i = 2; i < argc && n < 64; i++) a[n++] = atoll(argv[i]);
    (void)a;
    if (!strcmp(cmd, "answer")) {
        printf("42\n");
        return 0;
    }
    if (!strcmp(cmd, "burst")) {   // holdMs gapMs (now request pointer surface)...
        BurstState state{0, 0, -1};
        for (int i = 2; i + 3 < n; i += 4) {
            printf("%%d\n", (int)BurstDecide(&state, a[i], (int)a[i + 1], (int)a[0], (int)a[1],
                                             a[i + 2] != 0, a[i + 3] != 0));
        }
        return 0;
    }
    if (!strcmp(cmd, "delay")) {   // now lastKeptUp delayMs
        printf("%%d\n", HideDelayElapsed(a[0], a[1], (int)a[2]) ? 1 : 0);
        return 0;
    }
    if (!strcmp(cmd, "beyond")) {   // own(4) edge others(4 each)
        RECT own{(LONG)a[0], (LONG)a[1], (LONG)a[2], (LONG)a[3]};
        RECT others[8];
        int count = 0;
        for (int i = 5; i + 3 < n && count < 8; i += 4) {
            others[count++] = {(LONG)a[i], (LONG)a[i + 1], (LONG)a[i + 2], (LONG)a[i + 3]};
        }
        printf("%%d\n", DisplayBeyond(own, (UINT)a[4], others, count) ? 1 : 0);
        return 0;
    }
    if (!strcmp(cmd, "place")) {   // shownX shownY width height edge held hiddenPx
        BarPlacement p = PlaceBar({(LONG)a[0], (LONG)a[1]}, {(LONG)a[2], (LONG)a[3]}, (UINT)a[4],
                                  a[5] != 0, (int)a[6]);
        printf("%%d %%d %%ld %%ld %%ld %%ld %%d\n", p.x, p.y, p.region.left, p.region.top,
               p.region.right, p.region.bottom, p.clipped ? 1 : 0);
        return 0;
    }
    if (!strcmp(cmd, "strip")) {   // display(4) bar(4) edge widthPx dpi
        RECT r = RevealStrip({(LONG)a[0], (LONG)a[1], (LONG)a[2], (LONG)a[3]},
                             {(LONG)a[4], (LONG)a[5], (LONG)a[6], (LONG)a[7]}, (UINT)a[8], (int)a[9],
                             (UINT)a[10]);
        printf("%%ld %%ld %%ld %%ld\n", r.left, r.top, r.right, r.bottom);
        return 0;
    }
    if (!strcmp(cmd, "dwell")) {
        printf("%%d\n", HeldDwellMs((int)a[0]));
        return 0;
    }
    if (!strcmp(cmd, "fps")) {   // displayHz capFps
        printf("%%d\n", StepsPerSecond((int)a[0], (int)a[1]));
        return 0;
    }
    if (!strcmp(cmd, "panel")) {   // visible cloaked onDisplay
        printf("%%d\n", PanelCountsAsOpen(a[0] != 0, a[1] != 0, a[2] != 0) ? 1 : 0);
        return 0;
    }
    if (!strcmp(cmd, "diff")) {   // nHave have... nSeen seen...
        HWND have[16], seen[16], add[16], drop[16];
        int nHave = (int)a[0], nSeen = (int)a[1 + nHave], nDrop = 0;
        for (int i = 0; i < nHave; i++) have[i] = (HWND)(INT_PTR)a[1 + i];
        for (int i = 0; i < nSeen; i++) seen[i] = (HWND)(INT_PTR)a[2 + nHave + i];
        int nAdd = DiffWindows(have, nHave, seen, nSeen, add, drop, &nDrop);
        printf("%%d", nAdd);
        for (int i = 0; i < nAdd; i++) printf(" %%lld", (long long)(INT_PTR)add[i]);
        printf(" %%d", nDrop);
        for (int i = 0; i < nDrop; i++) printf(" %%lld", (long long)(INT_PTR)drop[i]);
        printf("\n");
        return 0;
    }
    if (!strcmp(cmd, "belongs")) {   // display(4) bar(4) edge
        printf("%%d\n", BarBelongsTo({(LONG)a[0], (LONG)a[1], (LONG)a[2], (LONG)a[3]},
                                     {(LONG)a[4], (LONG)a[5], (LONG)a[6], (LONG)a[7]},
                                     (UINT)a[8]) ? 1 : 0);
        return 0;
    }
    if (!strcmp(cmd, "heldstrip")) {
        printf("%%d\n", HeldStripPx((int)a[0]));
        return 0;
    }
    fprintf(stderr, "unknown command %%s\n", cmd);
    return 3;
}
'''


def extract_logic(src):
    m = re.search(r'(?ms)^// ==PureLogic==\n(.*?)^// ==/PureLogic==', src)
    assert m, 'PureLogic block not found in the mod'
    return m.group(1)


def build_logic(tmp_path):
    exe, _ = compile_harness(tmp_path, HARNESS % extract_logic(open(SRC).read()))

    def run(command, *args):
        p = subprocess.run([str(exe), command, *(str(int(a)) for a in args)], capture_output=True, text=True)
        assert p.returncode == 0, p.stderr or f'exit {p.returncode}'
        return [int(v) for v in p.stdout.split()]
    return run


@pytest.fixture(scope='module')
def logic(tmp_path_factory):
    return build_logic(tmp_path_factory.mktemp('logic'))


def test_block_is_found_and_compiles(logic):
    assert logic('answer') == [42]


# ---- notification bursts (#9) ------------------------------------------------
# Events are (now ms, request, pointer at the edge, shell surface in front).
# Verdicts: 0 begin a burst and show, 1 let Windows show it, 2 keep hidden.

HOLD, GAP = 1500, 10000


def burst(logic, *events):
    return logic('burst', HOLD, GAP, *[v for e in events for v in e])


def test_repeat_flashes_show_once(logic):
    # Request 17 every 5.6 s: shown once, then kept hidden.
    assert burst(logic, (0, 17, 0, 0), (5600, 17, 0, 0), (11200, 17, 0, 0)) == [0, 2, 2]


def test_stray_request_passes_without_starting_a_burst(logic):
    # A 15 between the 17s is shown, and the next 17 is still a repeat.
    assert burst(logic, (0, 17, 0, 0), (3000, 15, 0, 0), (5600, 17, 0, 0)) == [0, 1, 2]


def test_repeat_inside_its_hold_passes(logic):
    assert burst(logic, (0, 17, 0, 0), (1000, 17, 0, 0)) == [0, 1]


def test_quiet_longer_than_the_gap_begins_again(logic):
    assert burst(logic, (0, 17, 0, 0), (10001, 17, 0, 0)) == [0, 0]


def test_pointer_or_open_surface_lets_a_repeat_through(logic):
    assert burst(logic, (0, 17, 0, 0), (5600, 17, 1, 0)) == [0, 1]
    assert burst(logic, (0, 17, 0, 0), (5600, 17, 0, 1)) == [0, 1]


# ---- hide delay (#12) --------------------------------------------------------

def test_hide_delay(logic):
    # now, last moment something kept the taskbar up, delay
    assert logic('delay', 1000, 1000, 0) == [1]      # 0 hides at once
    assert logic('delay', 1499, 1000, 500) == [0]
    assert logic('delay', 1500, 1000, 500) == [1]


# ---- geometry: taskbars held on their own display (#11) ----------------------
# Edges: 0 left, 1 top, 2 right, 3 bottom. Rectangles are left, top, right, bottom.

PRIMARY, UPPER = (0, 0, 3440, 1440), (988, -2560, 2428, 0)   # the measured layout


def test_beyond(logic):
    def b(own, edge, *others):
        return logic('beyond', *own, edge, *[v for o in others for v in o])[0]
    assert b(UPPER, 3, PRIMARY) == 1 and b(PRIMARY, 3, UPPER) == 0   # stacked
    assert b(PRIMARY, 1, UPPER) == 1                                  # top of the lower one
    assert b((0, 0, 1920, 1080), 2, (1920, 0, 3840, 1080)) == 1       # side by side
    assert b((0, 0, 1920, 1080), 3, (1920, 0, 3840, 1080)) == 0
    assert b((0, 0, 1920, 1080), 2, (1920, 1000, 3840, 2080)) == 1    # partly facing counts
    assert b((0, 0, 1920, 1080), 2, (1920, 1080, 3840, 2160)) == 0    # corners only
    assert b((-1920, 0, 0, 1080), 0) == 0                             # negative coordinates, alone
    assert b((0, 0, 1920, 1080), 3, (1920, 0, 3840, 1080), (0, 1080, 1920, 2160)) == 1


def test_place_held_bottom_matches_the_spike(logic):
    def place(hidden):
        return logic('place', 988, -60, 1440, 60, 3, 1, hidden)
    assert place(0) == [988, -60, 0, 0, 0, 0, 0]                # shown, no region
    assert place(10) == [988, -50, 0, 0, 1440, 50, 1]           # sliding, overhang clipped
    assert place(58) == [988, -32, 0, 30, 1440, 32, 1]          # travel 28, 2 px left


def test_place_not_held_slides_all_the_way(logic):
    assert logic('place', 0, 1392, 3440, 48, 3, 0, 46) == [0, 1438, 0, 0, 3440, 2, 1]


def test_place_on_each_edge(logic):
    # 60 px thick, held, 58 hidden: the window travels 28 and 2 px stay, against the edge.
    assert logic('place', 0, 0, 60, 1080, 0, 1, 58) == [-28, 0, 28, 0, 30, 1080, 1]        # left
    assert logic('place', 0, 0, 1920, 60, 1, 1, 58) == [0, -28, 0, 28, 1920, 30, 1]        # top
    assert logic('place', 1860, 0, 60, 1080, 2, 1, 58) == [1888, 0, 30, 0, 32, 1080, 1]    # right


def test_held_window_is_never_mostly_off_its_display(logic):
    for edge, shown in ((0, (0, 0)), (1, (0, 0)), (2, (1860, 0)), (3, (0, 1020))):
        size = (60, 1080) if edge in (0, 2) else (1920, 60)
        for hidden in (0, 1, 27, 28, 29, 30, 58, 60):
            x, y, *_ = logic('place', *shown, *size, edge, 1, hidden)
            assert abs(x - shown[0]) + abs(y - shown[1]) <= 28


def test_strip_scales_with_dpi(logic):
    assert logic('strip', *UPPER, 988, -60, 2428, 0, 3, 8, 120) == [988, -10, 2428, 0]
    assert logic('strip', *PRIMARY, 0, 1392, 3440, 1440, 3, 8, 96) == [0, 1432, 3440, 1440]
    assert logic('strip', 0, 0, 1920, 1080, 0, 0, 48, 1080, 0, 8, 96) == [0, 0, 8, 1080]   # left


def test_dwell_and_rate(logic):
    assert [logic('dwell', v)[0] for v in (0, 150, 400)] == [150, 150, 400]
    assert [logic('fps', *v)[0] for v in ((60, 90), (240, 90), (240, 240), (0, 90))] == [60, 90, 240, 60]


# ---- Quick Settings (#10) ----------------------------------------------------

def test_panel_open_only_when_visible_uncloaked_and_on_a_display(logic):
    # visible, cloaked, on a display
    cases = [(v, c, d) for v in (0, 1) for c in (0, 1) for d in (0, 1)]
    assert [logic('panel', *case)[0] for case in cases] == [0, 0, 0, 0, 0, 1, 0, 0]


# ---- one record per taskbar window -------------------------------------------

def test_records_follow_windows(logic):
    # diff nHave have... nSeen seen...  ->  nAdd adds... nDrop drops...
    assert logic('diff', 2, 1, 2, 2, 2, 3) == [1, 3, 1, 1]   # have {1,2}, seen {2,3}
    assert logic('diff', 0, 2, 5, 6) == [2, 5, 6, 0]         # both new
    assert logic('diff', 2, 5, 6, 0) == [0, 2, 5, 6]         # both gone
    assert logic('diff', 2, 5, 6, 2, 6, 5) == [0, 0]         # same windows, other order


def test_a_taskbar_belongs_to_the_display_whose_edge_it_reaches(logic):
    # belongs display(4) bar(4) edge. Shown or hidden, the secondary measured
    # on the stacked layout is the upper display's, never the primary's.
    for bar in ((988, -60, 2428, 0), (988, -2, 2428, 58), (988, -32, 2428, 28)):
        assert logic('belongs', *UPPER, *bar, 3) == [1]
        assert logic('belongs', *PRIMARY, *bar, 3) == [0]
    for bar in ((0, 1392, 3440, 1440), (0, 1438, 3440, 1486)):
        assert logic('belongs', *PRIMARY, *bar, 3) == [1]
    # Two displays of one width, stacked: the taskbar straddling the seam is
    # the upper one's bottom taskbar, and the lower one's top taskbar.
    upper, lower, bar = (0, -1080, 1920, 0), (0, 0, 1920, 1080), (0, -2, 1920, 46)
    assert logic('belongs', *upper, *bar, 3) == [1] and logic('belongs', *lower, *bar, 3) == [0]
    assert logic('belongs', *lower, *bar, 1) == [1] and logic('belongs', *upper, *bar, 1) == [0]
    # Left and right edges.
    assert logic('belongs', 0, 0, 1920, 1080, -46, 0, 2, 1080, 0) == [1]
    assert logic('belongs', 0, 0, 1920, 1080, 1918, 0, 1966, 1080, 2) == [1]


def test_a_completely_hidden_taskbar_still_belongs_to_its_display(logic):
    # "Hide completely" parks the window just past the edge, touching it.
    assert logic('belongs', 0, 0, 3440, 1440, -48, 0, 0, 1440, 0) == [1]          # left
    assert logic('belongs', 0, 0, 3440, 1440, 0, -48, 3440, 0, 1) == [1]          # top
    assert logic('belongs', 0, 0, 3440, 1440, 3440, 0, 3488, 1440, 2) == [1]      # right
    assert logic('belongs', 0, 0, 3440, 1440, 0, 1440, 3440, 1488, 3) == [1]      # bottom
    # ...and not to the display it is parked on.
    assert logic('belongs', 3440, 0, 5360, 1440, 3440, 0, 3488, 1440, 2) == [0]


def test_held_strip_is_at_least_twelve_pixels(logic):
    # The pointer crosses a held edge at speed; 8 px was too thin to rest in.
    assert [logic('heldstrip', v)[0] for v in (1, 8, 12, 30)] == [12, 12, 12, 30]


def test_a_position_on_another_display_is_not_this_taskbars(logic):
    # After the upper display moved 28 px left, Windows asks for the taskbar
    # at its new place. That is not a position on the display the record knows.
    old_display, new_bar = (988, -2560, 2428, 0), (960, -2, 2400, 58)
    assert logic('belongs', *old_display, *new_bar, 3) == [0]
    assert logic('belongs', 960, -2560, 2400, 0, *new_bar, 3) == [1]
