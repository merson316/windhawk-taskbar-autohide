#!/usr/bin/env bash
# Read the z-order detector's log. Written by the shipping mod on every
# transition into and out of a demoted taskbar, so it survives Windhawk's log
# verbosity being turned off.
# Usage: tools/readzorder.sh [--clear|--tail]
set -uo pipefail

WINUSER="${WINUSER:-$(cmd.exe /c 'echo %USERNAME%' 2>/dev/null | tr -d '\r\n')}"
LOG="/mnt/c/Users/$WINUSER/AppData/Local/windhawk-autohide-zorder.log"

case "${1:-}" in
  --clear) : > "$LOG" 2>/dev/null && echo "cleared $LOG" || echo "not present: $LOG" ;;
  --tail)  tail -f "$LOG" ;;
  "")      if [ -s "$LOG" ]; then iconv -f UTF-16LE -t UTF-8 "$LOG" 2>/dev/null || cat "$LOG"
           else echo "no demotion recorded yet: $LOG" >&2; exit 1; fi ;;
  *)       echo "usage: readzorder.sh [--clear|--tail]" >&2; exit 2 ;;
esac
