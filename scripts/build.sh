#!/bin/bash
# FluxGraph Build Script (Linux/macOS)
#
# Usage:
#   ./scripts/build.sh [options]
#
# Options:
#   --clean                 Remove build directory before configuring
#   --config <type>         Build type: Release (default), Debug, or RelWithDebInfo
#   --server                Build gRPC server
#   --no-tests              Skip building tests
#   --json                  Enable JSON loader
#   --yaml                  Enable YAML loader
#   --generator <name>      CMake generator (default: Ninja if available)
#   -j, --jobs <N>          Parallel build jobs (default: auto)
#   -h, --help              Show this help

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

CONFIG="Release"
CLEAN=false
SERVER=false
NO_TESTS=false
JSON=false
YAML=false
GENERATOR=""
JOBS=""

usage() {
    sed -n '1,/^$/p' "$0" | grep -E '^#'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
    --clean)
        CLEAN=true
        shift
        ;;
    --config)
        CONFIG="$2"
        shift 2
        ;;
    --server)
        SERVER=true
        shift
        ;;
    --no-tests)
        NO_TESTS=true
        shift
        ;;
    --json)
        JSON=true
        shift
        ;;
    --yaml)
        YAML=true
        shift
        ;;
    --generator)
        GENERATOR="$2"
        shift 2
        ;;
    -j | --jobs)
        JOBS="$2"
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
echo "FluxGraph Build Script"
echo "============================================"
echo "Config:      $CONFIG"
echo "Server:      $SERVER"
echo "Tests:       $([ "$NO_TESTS" = false ] && echo true || echo false)"
echo "JSON:        $JSON"
echo "YAML:        $YAML"
echo "Clean:       $CLEAN"
echo "============================================"

# Build directory naming
BUILD_DIR_SUFFIX="${CONFIG,,}"
[ "$SERVER" = true ] && BUILD_DIR_SUFFIX="${BUILD_DIR_SUFFIX}-server"
BUILD_DIR="$REPO_ROOT/build-$BUILD_DIR_SUFFIX"

# Clean if requested
if [ "$CLEAN" = true ] && [ -d "$BUILD_DIR" ]; then
    echo ""
    echo "[CLEAN] Removing $BUILD_DIR..."
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Auto-detect CMake generator
if [ -z "$GENERATOR" ]; then
    if command -v ninja &>/dev/null; then
        GENERATOR="Ninja"
    fi
fi

# CMake arguments
CMAKE_ARGS=(
    "-DCMAKE_BUILD_TYPE=$CONFIG"
)

if [ -n "$GENERATOR" ]; then
    CMAKE_ARGS+=("-G" "$GENERATOR")
fi

# vcpkg toolchain
if [ -n "${VCPKG_ROOT:-}" ]; then
    CMAKE_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake")
    echo "  Using vcpkg: $VCPKG_ROOT"
fi

# Build options
if [ "$NO_TESTS" = false ]; then
    CMAKE_ARGS+=("-DFLUXGRAPH_BUILD_TESTS=ON")
else
    CMAKE_ARGS+=("-DFLUXGRAPH_BUILD_TESTS=OFF")
fi

[ "$SERVER" = true ] && CMAKE_ARGS+=("-DFLUXGRAPH_BUILD_SERVER=ON")
[ "$JSON" = true ] && CMAKE_ARGS+=("-DFLUXGRAPH_JSON_ENABLED=ON")
[ "$YAML" = true ] && CMAKE_ARGS+=("-DFLUXGRAPH_YAML_ENABLED=ON")

CMAKE_ARGS+=("..")

# Configure
echo ""
echo "[CMAKE] Configuring..."
cmake "${CMAKE_ARGS[@]}"

# Build
echo ""
echo "[BUILD] Compiling ($CONFIG)..."
if [ -n "$JOBS" ]; then
    cmake --build . --config "$CONFIG" -j "$JOBS"
else
    cmake --build . --config "$CONFIG"
fi

# Run tests if enabled
if [ "$NO_TESTS" = false ]; then
    echo ""
    echo "[TEST] Running tests..."
    if ! ctest -C "$CONFIG" --output-on-failure; then
        echo ""
        echo "[WARNING] Some tests failed"
    else
        echo ""
        echo "[SUCCESS] All tests passed!"
    fi
fi

echo ""
echo "============================================"
echo "Build completed successfully!"
echo "Build directory: $BUILD_DIR"
echo "============================================"
