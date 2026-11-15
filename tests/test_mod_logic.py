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
