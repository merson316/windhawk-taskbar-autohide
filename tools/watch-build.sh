#!/usr/bin/env bash
# Sign-in check for a new taskbar.dll that needs built-in addresses.
# Usage: tools/watch-build.sh [--dry-run]   (run from the repo root)
set -uo pipefail
cd "$(dirname "$0")/.."
exec .venv/bin/python tools/watch_build.py "$@"
