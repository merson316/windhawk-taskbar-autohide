#!/usr/bin/env bash
# Pre-flight compile a Windhawk mod source using Windhawk's own clang.
# Usage: tools/whcompile.sh <file.wh.cpp>
set -uo pipefail

SRC="${1:?usage: whcompile.sh <file.wh.cpp>}"
[ -f "$SRC" ] || { echo "no such file: $SRC" >&2; exit 2; }

CLANG="/mnt/c/Program Files/Windhawk/Compiler/bin/clang++.exe"
INCLUDE='C:\Program Files\Windhawk\Compiler\include'
[ -x "$CLANG" ] || { echo "Windhawk clang not found at $CLANG" >&2; exit 2; }

# The compiler is a Windows binary, so the source must sit on a Windows volume.
STAGE_WIN='C:\Users\<user>\AppData\Local\Temp'
STAGE_WSL='/mnt/c/Users/<user>/AppData/Local/Temp'
BASE="whcompile_$$.cpp"
cp "$SRC" "$STAGE_WSL/$BASE" || exit 2
trap 'rm -f "$STAGE_WSL/$BASE"' EXIT

"$CLANG" -fsyntax-only \
  -x c++ -std=c++23 -target x86_64-w64-mingw32 \
  -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 \
  -D_WIN32_IE=0x0A00 -DNTDDI_VERSION=0x0A000008 \
  -D__USE_MINGW_ANSI_STDIO=0 -DWH_MOD -DWH_EDITING \
  -I "$INCLUDE" -include windhawk_api.h \
  -Wno-pragma-pack -Wno-pragma-system-header-outside-header \
  "$STAGE_WIN\\$BASE" 2>&1 | sed "s|[^ ]*$BASE|$SRC|g"

exit "${PIPESTATUS[0]}"
