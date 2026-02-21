#!/bin/bash
# FluxGraph test wrapper (preset-first)
#
# Usage:
#   ./scripts/test.sh [--preset <name>] [--verbose]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

PRESET="dev-release"
VERBOSE=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      PRESET="$2"
      shift 2
      ;;
    --verbose)
      VERBOSE=true
      shift
      ;;
    -h|--help)
      sed -n '1,10p' "$0"
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      sed -n '1,10p' "$0"
      exit 1
      ;;
  esac
done

cd "$REPO_ROOT"

CTEST_ARGS=(--preset "$PRESET")
if [[ "$VERBOSE" == "true" ]]; then
  CTEST_ARGS+=(--verbose)
fi

echo "[TEST] ctest --preset $PRESET"
ctest "${CTEST_ARGS[@]}"
