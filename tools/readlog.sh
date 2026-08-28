#!/usr/bin/env bash
# Read the phase 1 diagnostic log.
# Usage: tools/readlog.sh [--clear|--tail]
set -uo pipefail

LOG="/mnt/c/Users/<user>/AppData/Local/windhawk-autohide-diag.log"

case "${1:-}" in
  --clear) : > "$LOG" 2>/dev/null && echo "cleared $LOG" || echo "not present: $LOG"; ;;
  --tail)  tail -f "$LOG" ;;
  "")      if [ -f "$LOG" ]; then cat "$LOG"; else echo "not present: $LOG" >&2; exit 1; fi ;;
  *)       echo "usage: readlog.sh [--clear|--tail]" >&2; exit 2 ;;
esac
