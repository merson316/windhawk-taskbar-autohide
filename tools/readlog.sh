#!/usr/bin/env bash
# Read the phase 1 diagnostic log.
# Usage: tools/readlog.sh [--clear|--tail]
set -uo pipefail

# Resolve the Windows user rather than hardcoding it.
WINUSER="${WINUSER:-$(cmd.exe /c 'echo %USERNAME%' 2>/dev/null | tr -d '\r\n')}"
LOCALAPP="/mnt/c/Users/$WINUSER/AppData/Local"

LOG="$LOCALAPP/windhawk-autohide-diag.log"

case "${1:-}" in
  --clear) : > "$LOG" 2>/dev/null && echo "cleared $LOG" || echo "not present: $LOG"; ;;
  --tail)  tail -f "$LOG" ;;
  "")      if [ -f "$LOG" ]; then cat "$LOG"; else echo "not present: $LOG" >&2; exit 1; fi ;;
  *)       echo "usage: readlog.sh [--clear|--tail]" >&2; exit 2 ;;
esac
