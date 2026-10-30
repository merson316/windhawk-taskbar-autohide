#!/usr/bin/env bash
# Compiles the mod exactly as Windhawk does when you click Compile, with
# Windhawk's own compiler, so a build error shows up before a release does.
# Usage: tools/compile-mod.sh [file.wh.cpp] [output.dll]   (default: the shipped mod)
# Runs from WSL or from Git Bash on Windows (as in CI). WINDHAWK_DIR overrides
# where Windhawk lives: the folder holding its Compiler and Engine folders.
set -euo pipefail
SRC="$(realpath "${1:-$(dirname "$0")/../src/taskbar-autohide-any-edge.wh.cpp}")"
OUT="$(realpath -m "${2:-$(mktemp -d)/mod.dll}")"
WH="${WINDHAWK_DIR:-/mnt/c/Program Files/Windhawk}"

winpath() { if command -v wslpath >/dev/null; then wslpath -w "$1"; else cygpath -w "$1"; fi; }
meta() { sed -n "s|^// @$1[[:space:]]\{1,\}||p" "$SRC" | head -1 | tr -d '\r'; }

ENGINE="$(ls -d "$WH"/Engine/*/64 | sort -V | tail -1)"
read -ra OPTS <<< "$(meta compilerOptions)"

# The argument list is Windhawk's compileModInternal, in its order.
cd "$WH/Compiler"
./bin/clang++.exe -std=c++23 -O2 -shared -DUNICODE -D_UNICODE \
    -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -D_WIN32_IE=0x0A00 -DNTDDI_VERSION=0x0A000008 \
    -D__USE_MINGW_ANSI_STDIO=0 -DWH_MOD \
    "-DWH_MOD_ID=L\"$(meta id)\"" "-DWH_MOD_VERSION=L\"$(meta version)\"" \
    "$(winpath "$ENGINE/windhawk.lib")" -x c++ - -include windhawk_api.h \
    -target x86_64-w64-mingw32 -Wl,--export-all-symbols -o "$(winpath "$OUT")" \
    "${OPTS[@]}" < "$SRC"
echo "compiled $(meta id) $(meta version) with $(basename "$(dirname "$ENGINE")"): $OUT"
