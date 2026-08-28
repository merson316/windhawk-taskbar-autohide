#!/usr/bin/env bash
# Truncate the mod logs in place.
#
# Never delete these files: the mod opens them with FILE_SHARE_DELETE, so an
# unlink succeeds while the mod keeps writing to the now-orphaned inode. The
# log then never reappears and a whole test run is lost. Truncation keeps the
# mod's handle valid.
set -uo pipefail

# Resolve the Windows user rather than hardcoding it.
WINUSER="${WINUSER:-$(cmd.exe /c 'echo %USERNAME%' 2>/dev/null | tr -d '\r\n')}"
LOCALAPP="/mnt/c/Users/$WINUSER/AppData/Local"
for f in "$LOCALAPP/windhawk-autohide-fix.log" \
         "$LOCALAPP/windhawk-autohide-diag.log"; do
  if [ -f "$f" ]; then
    : > "$f" && echo "truncated $(basename "$f")"
  else
    echo "absent (will be created on mod load): $(basename "$f")"
  fi
done
