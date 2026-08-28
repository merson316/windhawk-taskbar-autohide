#!/usr/bin/env bash
# Truncate the mod logs in place.
#
# Never delete these files: the mod opens them with FILE_SHARE_DELETE, so an
# unlink succeeds while the mod keeps writing to the now-orphaned inode. The
# log then never reappears and a whole test run is lost. Truncation keeps the
# mod's handle valid.
set -uo pipefail
for f in /mnt/c/Users/<user>/AppData/Local/windhawk-autohide-fix.log \
         /mnt/c/Users/<user>/AppData/Local/windhawk-autohide-diag.log; do
  if [ -f "$f" ]; then
    : > "$f" && echo "truncated $(basename "$f")"
  else
    echo "absent (will be created on mod load): $(basename "$f")"
  fi
done
