#!/usr/bin/env bash
# Pre-flight COMPILE AND LINK of a Windhawk mod using Windhawk's own clang.
# A syntax-only check cannot see missing libraries, so this links a real DLL
# and honours the mod's @compilerOptions line, exactly as Windhawk does.
# Usage: tools/whcompile.sh <file.wh.cpp>
set -uo pipefail

# Resolve the Windows user rather than hardcoding it.
WINUSER="${WINUSER:-$(cmd.exe /c 'echo %USERNAME%' 2>/dev/null | tr -d '\r\n')}"
LOCALAPP="/mnt/c/Users/$WINUSER/AppData/Local"

SRC="${1:?usage: whcompile.sh <file.wh.cpp>}"
[ -f "$SRC" ] || { echo "no such file: $SRC" >&2; exit 2; }

CLANG="/mnt/c/Program Files/Windhawk/Compiler/bin/clang++.exe"
INCLUDE='C:\Program Files\Windhawk\Compiler\include'
[ -x "$CLANG" ] || { echo "Windhawk clang not found at $CLANG" >&2; exit 2; }

STAGE_WIN="C:\\Users\\$WINUSER\\AppData\\Local\\Temp"
STAGE_WSL="$LOCALAPP/Temp"
BASE="whcompile_$$.cpp"
OUT="whcompile_$$.dll"
cp "$SRC" "$STAGE_WSL/$BASE" || exit 2
trap 'rm -f "$STAGE_WSL/$BASE" "$STAGE_WSL/$OUT"' EXIT

# Mods declare their own link libraries in the metadata block.
MODOPTS=$(sed -n 's|^// @compilerOptions[[:space:]]*\(.*\)$|\1|p' "$SRC" | head -1)

# shellcheck disable=SC2086
"$CLANG" -shared \
  -x c++ -std=c++23 -target x86_64-w64-mingw32 \
  -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 \
  -D_WIN32_IE=0x0A00 -DNTDDI_VERSION=0x0A000008 \
  -D__USE_MINGW_ANSI_STDIO=0 -DWH_MOD -DWH_EDITING \
  -I "$INCLUDE" -include windhawk_api.h \
  -Wno-pragma-pack -Wno-pragma-system-header-outside-header \
  "$STAGE_WIN\\$BASE" $MODOPTS \
  -o "$STAGE_WIN\\$OUT" 2>&1 | sed "s|[^ ]*$BASE|$SRC|g"

exit "${PIPESTATUS[0]}"
