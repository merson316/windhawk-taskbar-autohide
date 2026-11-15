"""The debug build of the mod: the same source with its detailed log switched
on. One line differs, and nothing else.

    tools/make_debug_build.py [mod.wh.cpp] [out.wh.cpp]     (default: the shipped mod, to stdout)
"""
import os
import sys

OFF = '// #define TASKBAR_AUTOHIDE_DEBUG\n'
ON = '#define TASKBAR_AUTOHIDE_DEBUG\n'
SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'taskbar-autohide-any-edge.wh.cpp')


def has_switch(source):
    return source.count(OFF) == 1


def debug_source(source):
    """source with the detailed log on. Raises ValueError when the source has
    no switch for it, or more than one."""
    if not has_switch(source):
        raise ValueError(f'expected exactly one line {OFF.strip()!r}')
    return source.replace(OFF, ON)


def main(argv):
    with open(argv[1] if len(argv) > 1 else SRC, newline='') as f:
        source = f.read()
    try:
        out = debug_source(source)
    except ValueError as e:
        sys.exit(f'no debug build: {e}')
    if len(argv) > 2:
        with open(argv[2], 'w', newline='') as f:
            f.write(out)
    else:
        sys.stdout.write(out)


if __name__ == '__main__':
    main(sys.argv)
