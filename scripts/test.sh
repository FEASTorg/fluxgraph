#!/bin/bash
# FluxGraph Test Script (Linux/macOS)
#
# Usage:
#   ./scripts/test.sh [options]
#
# Options:
#   --config <type>         Build config: Release (default), Debug, or RelWithDebInfo
#   --integration           Run integration tests (requires server build)
#   --verbose               Verbose test output
#   -h, --help              Show this help

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

CONFIG="Release"
INTEGRATION=false
VERBOSE=false

usage() {
    sed -n '1,/^$/p' "$0" | grep -E '^#'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
    --config)
        CONFIG="$2"
        shift 2
        ;;
    --integration)
        INTEGRATION=true
        shift
        ;;
    --verbose)
        VERBOSE=true
        shift
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
echo "FluxGraph Test Runner"
echo "============================================"

# Find build directory
BUILD_DIRS=(
    "build-${CONFIG,,}-server"
    "build-${CONFIG,,}"
    "build-server"
    "build-core"
    "build-json"
    "build-yaml"
    "build-both"
)

BUILD_DIR=""
for dir in "${BUILD_DIRS[@]}"; do
    if [ -d "$REPO_ROOT/$dir" ]; then
        BUILD_DIR="$REPO_ROOT/$dir"
        break
    fi
done

if [ -z "$BUILD_DIR" ]; then
    echo "[ERROR] No build directory found. Run build.sh first."
    exit 1
fi

echo "Build dir:   $BUILD_DIR"
echo "Integration: $INTEGRATION"
echo "============================================"

cd "$BUILD_DIR"

# Run C++ unit tests
echo ""
echo "[TEST] Running C++ unit tests..."

CTEST_ARGS=("-C" "$CONFIG" "--output-on-failure")
[ "$VERBOSE" = true ] && CTEST_ARGS+=("-V")

if ! ctest "${CTEST_ARGS[@]}"; then
    echo ""
    echo "[FAILED] Some C++ tests failed"
    exit 1
fi

echo ""
echo "[SUCCESS] C++ tests passed!"

# Run integration tests if requested
if [ "$INTEGRATION" = true ]; then
    echo ""
    echo "[TEST] Running integration tests..."
    
    # Check for venv
    VENV_PATH="$REPO_ROOT/.venv-fxg"
    if [ ! -d "$VENV_PATH" ]; then
        echo "[WARNING] Python venv not found at: $VENV_PATH"
        echo "  Create venv: python3 -m venv .venv-fxg"
       echo "  Install deps: .venv-fxg/bin/pip install -r requirements.txt"
        exit 0
    fi
    
    # Ensure protobuf bindings are generated
    PROTO_PYTHON_DIR="$REPO_ROOT/build-server/python"
    if [ ! -f "$PROTO_PYTHON_DIR/fluxgraph_pb2.py" ]; then
        echo "  Generating Python protobuf bindings..."
        "$REPO_ROOT/scripts/generate-proto-python.sh"
    fi
    
    # Find server executable
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
        echo "[WARNING] Server not built. Build with: ./scripts/build.sh --server --config $CONFIG"
        exit 0
    fi
    
    # Start server in background
    echo "  Starting server: $SERVER_EXE"
    "$SERVER_EXE" --port 50051 &
    SERVER_PID=$!
    
    # Ensure server is killed on exit
    trap "kill $SERVER_PID 2>/dev/null || true" EXIT
    
    sleep 2
    
    # Run Python integration tests
    PYTHON_EXE="$VENV_PATH/bin/python3"
    TEST_SCRIPT="$REPO_ROOT/tests/test_grpc_integration.py"
    
    if ! "$PYTHON_EXE" "$TEST_SCRIPT"; then
        echo ""
        echo "[FAILED] Integration tests failed"
        exit 1
    fi
    
    echo ""
    echo "[SUCCESS] Integration tests passed!"
    
    # Stop server
    kill $SERVER_PID 2>/dev/null || true
fi

echo ""
echo "============================================"
echo "All tests passed!"
echo "============================================"
