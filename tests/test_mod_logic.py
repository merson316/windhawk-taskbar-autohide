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
