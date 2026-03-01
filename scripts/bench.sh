#!/usr/bin/env bash
# FluxGraph benchmark wrapper (preset-first)
# Usage:
#   ./scripts/bench.sh [--preset <name>] [--config <cfg>] [--output-dir <path>] [--include-optional] [--no-build] [--fail-on-status]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PRESET="dev-release"
CONFIG=""
OUTPUT_DIR=""
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      PRESET="$2"
      shift 2
      ;;
    --config)
      CONFIG="$2"
      shift 2
      ;;
    --output-dir)
      OUTPUT_DIR="$2"
      shift 2
      ;;
    --include-optional|--no-build|--fail-on-status)
      EXTRA_ARGS+=("$1")
      shift
      ;;
    -h|--help)
      echo "Usage: $0 [--preset <name>] [--config <cfg>] [--output-dir <path>] [--include-optional] [--no-build] [--fail-on-status]"
      exit 0
      ;;
    *)
      EXTRA_ARGS+=("$1")
      shift
      ;;
  esac
done

if [[ -n "${VCPKG_ROOT:-}" ]]; then
  :
else
  echo "WARNING: VCPKG_ROOT is not set. Presets may fail to configure." >&2
fi

CMD=(python3 "$SCRIPT_DIR/run_benchmarks.py" --preset "$PRESET")
if [[ -n "$CONFIG" ]]; then
  CMD+=(--config "$CONFIG")
fi
if [[ -n "$OUTPUT_DIR" ]]; then
  CMD+=(--output-dir "$OUTPUT_DIR")
fi
CMD+=("${EXTRA_ARGS[@]}")

cd "$REPO_ROOT"
echo "[BENCH] ${CMD[*]}"
"${CMD[@]}"
