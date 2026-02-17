#!/bin/bash
# FluxGraph Server Runner (Linux/macOS)
#
# Usage:
#   ./scripts/run-server.sh [options]
#
# Options:
#   --config <type>         Build config: Release (default), Debug, or RelWithDebInfo
#   --port <port>           Server port (default: 50051)
#   --config-file <path>    Preload config file (YAML or JSON)
#   --dt <seconds>          Timestep in seconds (default: 0.1)
#   -h, --help              Show this help

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

CONFIG="Release"
PORT=50051
CONFIG_FILE=""
TIMESTEP=0.1

usage() {
    sed -n '1,/^$/p' "$0" | grep -E '^#'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
    --config)
        CONFIG="$2"
        shift 2
        ;;
    --port)
        PORT="$2"
        shift 2
        ;;
    --config-file)
        CONFIG_FILE="$2"
        shift 2
        ;;
    --dt)
        TIMESTEP="$2"
        shift 2
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    *)
        echo "Unknown option: $1"
        usage
        exit 1
        ;;
    esac
done

echo "============================================"
echo "FluxGraph gRPC Server"
echo "============================================"

# Find server executable (check multiple possible build directories)
SERVER_EXE=""
POSSIBLE_PATHS=(
    "$REPO_ROOT/build-${CONFIG,,}-server/server/fluxgraph-server"
    "$REPO_ROOT/build-server/server/Release/fluxgraph-server"
    "$REPO_ROOT/build/server/fluxgraph-server"
)

for path in "${POSSIBLE_PATHS[@]}"; do
    if [ -f "$path" ]; then
        SERVER_EXE="$path"
        break
    fi
done

if [ -z "$SERVER_EXE" ]; then
    echo "[ERROR] Server executable not found. Tried:"
    for path in "${POSSIBLE_PATHS[@]}"; do
        echo "  - $path"
    done
    echo ""
    echo "Build the server first: ./scripts/build.sh --server --config $CONFIG"
    exit 1
fi

# Build arguments
ARGS=("--port" "$PORT" "--dt" "$TIMESTEP")

if [ -n "$CONFIG_FILE" ]; then
    if [ ! -f "$CONFIG_FILE" ]; then
        echo "[ERROR] Config file not found: $CONFIG_FILE"
        exit 1
    fi
    ARGS+=("--config" "$CONFIG_FILE")
fi

echo "Port:        $PORT"
echo "Timestep:    $TIMESTEP sec"
[ -n "$CONFIG_FILE" ] && echo "Config:      $CONFIG_FILE"
echo "============================================"
echo ""

# Run server
"$SERVER_EXE" "${ARGS[@]}"
