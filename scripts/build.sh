#!/bin/bash
# FluxGraph build wrapper (preset-first)
#
# Usage:
#   ./scripts/build.sh [--preset <name>] [--clean-first] [-j <N>]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

PRESET="dev-release"
CLEAN_FIRST=false
JOBS=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      PRESET="$2"
      shift 2
      ;;
    --clean-first)
      CLEAN_FIRST=true
      shift
      ;;
    -j|--jobs)
      JOBS="$2"
      shift 2
      ;;
    -h|--help)
      sed -n '1,12p' "$0"
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      sed -n '1,12p' "$0"
      exit 1
      ;;
  esac
done

if [[ -z "${VCPKG_ROOT:-}" ]]; then
  echo "ERROR: VCPKG_ROOT is not set. Presets expect vcpkg toolchain at \$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" >&2
  exit 1
fi

cd "$REPO_ROOT"

echo "[CONFIGURE] cmake --preset $PRESET"
cmake --preset "$PRESET"

echo "[BUILD] cmake --build --preset $PRESET"
BUILD_ARGS=(--preset "$PRESET")
if [[ "$CLEAN_FIRST" == "true" ]]; then
  BUILD_ARGS+=(--clean-first)
fi
if [[ -n "$JOBS" ]]; then
  BUILD_ARGS+=(--parallel "$JOBS")
fi
cmake --build "${BUILD_ARGS[@]}"
