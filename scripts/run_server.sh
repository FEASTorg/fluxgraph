#!/bin/bash
# FluxGraph Server Runner (Linux/macOS)
#
# Usage:
#   ./scripts/run_server.sh [options]
#
# Options:
#   --preset <name>       Build preset (default: ci-linux-release-server)
#   --build-dir <path>    Build directory override (default: build/<preset>, except ci-linux-release-server -> build-server)
#   --port <port>         Server port (default: 50051)
#   --config-file <path>  Preload config file (YAML or JSON)
#   --dt <seconds>        Timestep in seconds (default: 0.1)
#   -h, --help            Show this help

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

PRESET="ci-linux-release-server"
BUILD_DIR=""
PORT=50051
CONFIG_FILE=""
TIMESTEP=0.1

usage() {
    sed -n '1,/^$/p' "$0" | grep -E '^#'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
    --preset)
        [[ $# -lt 2 ]] && {
            echo "[ERROR] --preset requires a value"
            exit 2
        }
        PRESET="$2"
        shift 2
        ;;
    --preset=*)
        PRESET="${1#*=}"
        shift
        ;;
    --build-dir)
        [[ $# -lt 2 ]] && {
            echo "[ERROR] --build-dir requires a value"
            exit 2
        }
        BUILD_DIR="$2"
        shift 2
        ;;
    --build-dir=*)
        BUILD_DIR="${1#*=}"
        shift
        ;;
    --port)
        [[ $# -lt 2 ]] && {
            echo "[ERROR] --port requires a value"
            exit 2
        }
        PORT="$2"
        shift 2
        ;;
    --config-file)
        [[ $# -lt 2 ]] && {
            echo "[ERROR] --config-file requires a value"
            exit 2
        }
        CONFIG_FILE="$2"
        shift 2
        ;;
    --dt)
        [[ $# -lt 2 ]] && {
            echo "[ERROR] --dt requires a value"
            exit 2
        }
        TIMESTEP="$2"
        shift 2
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    *)
        echo "[ERROR] Unknown option: $1"
        usage
        exit 1
        ;;
    esac
done

if [[ -z "$BUILD_DIR" ]]; then
    if [[ "$PRESET" == "ci-linux-release-server" ]]; then
        BUILD_DIR="$REPO_ROOT/build-server"
    else
        BUILD_DIR="$REPO_ROOT/build/$PRESET"
    fi
fi

echo "============================================"
echo "FluxGraph gRPC Server"
echo "============================================"

SERVER_EXE=""
POSSIBLE_PATHS=(
    "$BUILD_DIR/server/fluxgraph-server"
    "$BUILD_DIR/server/Release/fluxgraph-server"
    "$BUILD_DIR/server/Debug/fluxgraph-server"
)

for path in "${POSSIBLE_PATHS[@]}"; do
    if [[ -x "$path" ]]; then
        SERVER_EXE="$path"
        break
    fi
done

if [[ -z "$SERVER_EXE" ]]; then
    echo "[ERROR] Server executable not found. Tried:"
    for path in "${POSSIBLE_PATHS[@]}"; do
        echo "  - $path"
    done
    echo ""
    echo "Build first: ./scripts/build.sh --preset $PRESET"
    exit 1
fi

ARGS=("--port" "$PORT" "--dt" "$TIMESTEP")

if [[ -n "$CONFIG_FILE" ]]; then
    if [[ ! -f "$CONFIG_FILE" ]]; then
        echo "[ERROR] Config file not found: $CONFIG_FILE"
        exit 1
    fi
    ARGS+=("--config" "$CONFIG_FILE")
fi

echo "Preset:      $PRESET"
echo "Build dir:   $BUILD_DIR"
echo "Port:        $PORT"
echo "Timestep:    $TIMESTEP sec"
[ -n "$CONFIG_FILE" ] && echo "Config:      $CONFIG_FILE"
echo "============================================"
echo ""

"$SERVER_EXE" "${ARGS[@]}"
