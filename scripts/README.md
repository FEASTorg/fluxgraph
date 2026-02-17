# FluxGraph Convenience Scripts

This directory contains build, test, and deployment scripts for FluxGraph.

## Overview

- **build** - Build the project with various configurations
- **test** - Run unit and integration tests
- **run-server** - Start the gRPC server
- **generate-proto-python** - Generate Python bindings for testing

All scripts have Windows (`.ps1`) and Linux/macOS (`.sh`) variants.

---

## Build Scripts

### Windows

```powershell
.\scripts\build.ps1 [options]

Options:
  -Clean                  Remove build directory before configuring
  -Config <type>          Release (default), Debug, or RelWithDebInfo
  -Server                 Build gRPC server
  -NoTests                Skip building tests
  -JSON                   Enable JSON loader
  -YAML                   Enable YAML loader
```

**Examples:**

```powershell
# Core library only (Release)
.\scripts\build.ps1

# Server build with JSON/YAML support
.\scripts\build.ps1 -Server -JSON -YAML

# Debug build without tests
.\scripts\build.ps1 -Config Debug -NoTests

# Clean rebuild
.\scripts\build.ps1 -Clean -Server -JSON -YAML
```

### Linux/macOS

```bash
./scripts/build.sh [options]

Options:
  --clean                 Remove build directory before configuring
  --config <type>         Release (default), Debug, or RelWithDebInfo
  --server                Build gRPC server
  --no-tests              Skip building tests
  --json                  Enable JSON loader
  --yaml                  Enable YAML loader
  --generator <name>      CMake generator (Ninja, Unix Makefiles, etc.)
  -j, --jobs <N>          Parallel build jobs
```

**Examples:**

```bash
# Core library only
./scripts/build.sh

# Server build with all features
./scripts/build.sh --server --json --yaml

# Parallel build with Ninja
./scripts/build.sh --server --generator Ninja -j 8

# Clean debug build
./scripts/build.sh --clean --config Debug
```

---

## Test Scripts

### Windows

```powershell
.\scripts\test.ps1 [options]

Options:
  -Config <type>          Release (default), Debug, or RelWithDebInfo
  -Integration            Run integration tests (requires server build)
  -Verbose                Verbose test output
```

**Examples:**

```powershell
# Run unit tests only
.\scripts\test.ps1

# Run all tests including integration
.\scripts\test.ps1 -Integration

# Verbose debug tests
.\scripts\test.ps1 -Config Debug -Verbose
```

### Linux/macOS

```bash
./scripts/test.sh [options]

Options:
  --config <type>         Release (default), Debug, or RelWithDebInfo
  --integration           Run integration tests
  --verbose               Verbose test output
```

**Examples:**

```bash
# Run unit tests only
./scripts/test.sh

# Run all tests
./scripts/test.sh --integration

# Verbose output
./scripts/test.sh --verbose
```

---

## Server Scripts

### Windows

```powershell
.\scripts\run-server.ps1 [options]

Options:
  -Config <type>          Release (default), Debug, or RelWithDebInfo
  -Port <port>            Server port (default: 50051)
  -ConfigFile <path>      Preload config file (YAML or JSON)
  -TimeStep <seconds>     Timestep in seconds (default: 0.1)
```

**Examples:**

```powershell
# Start server on default port
.\scripts\run-server.ps1

# Start with preloaded config
.\scripts\run-server.ps1 -ConfigFile examples\04_yaml_graph\graph.yaml

# Custom port and timestep
.\scripts\run-server.ps1 -Port 50052 -TimeStep 0.05
```

### Linux/macOS

```bash
./scripts/run-server.sh [options]

Options:
  --config <type>         Release (default), Debug, or RelWithDebInfo
  --port <port>           Server port (default: 50051)
  --config-file <path>    Preload config file
  --dt <seconds>          Timestep in seconds (default: 0.1)
```

**Examples:**

```bash
# Start server on default port
./scripts/run-server.sh

# Start with preloaded config
./scripts/run-server.sh --config-file examples/04_yaml_graph/graph.yaml

# Custom settings
./scripts/run-server.sh --port 50052 --dt 0.05
```

---

## Proto Generation Scripts

Generate Python bindings for gRPC testing and client development.

### Windows

```powershell
.\scripts\generate-proto-python.ps1 [-OutputDir <path>]
```

**Example:**

```powershell
# Generate to tests/ directory (default)
.\scripts\generate-proto-python.ps1

# Generate to custom directory
.\scripts\generate-proto-python.ps1 -OutputDir client\python
```

### Linux/macOS

```bash
./scripts/generate-proto-python.sh [output-dir]
```

**Example:**

```bash
# Generate to tests/ directory (default)
./scripts/generate-proto-python.sh

# Generate to custom directory
./scripts/generate-proto-python.sh client/python
```

---

## Common Workflows

### Development Workflow

```bash
# 1. Build core library with JSON/YAML support
./scripts/build.sh --json --yaml

# 2. Run tests
./scripts/test.sh

# 3. Build server
./scripts/build.sh --server --json --yaml

# 4. Run integration tests
./scripts/test.sh --integration
```

### Clean Rebuild

```bash
# Clean and rebuild everything
./scripts/build.sh --clean --server --json --yaml
./scripts/test.sh --integration
```

### Server Deployment

```bash
# 1. Build release server
./scripts/build.sh --config Release --server --json --yaml --no-tests

# 2. Test server
./scripts/run-server.sh --config-file examples/04_yaml_graph/graph.yaml

# 3. Install (from build directory)
cd build-release-server
sudo cmake --install .
```

---

## Prerequisites

### Build Requirements

- CMake 3.20+
- C++17 compiler (MSVC 2019+, GCC 9+, Clang 10+)
- vcpkg (for gRPC server builds)

### Test Requirements

- Python 3.8+ (for integration tests)
- grpcio and grpcio-tools (install via `pip install -r requirements.txt`)

### Environment Variables

- `VCPKG_ROOT` - Path to vcpkg installation (required for server builds)

---

## Troubleshooting

### Build Failures

**Problem:** CMake can't find gRPC or protobuf

**Solution:** Ensure `VCPKG_ROOT` is set and vcpkg toolchain is accessible:

```bash
export VCPKG_ROOT=/path/to/vcpkg  # Linux/macOS
$env:VCPKG_ROOT = "C:\tools\vcpkg"  # Windows
```

### Integration Test Failures

**Problem:** Tests can't connect to server

**Solution:** Ensure server is built and port 50051 is not in use:

```bash
# Check if port is in use
netstat -ano | grep 50051  # Linux
netstat -ano | findstr 50051  # Windows
```

### Proto Generation Failures

**Problem:** `grpc_tools.protoc` module not found

**Solution:** Install Python gRPC tools:

```bash
pip install grpcio-tools
```

---

## Script Conventions

- All scripts check for errors and exit with non-zero status on failure
- Build artifacts are placed in `build-<config>[-suffix]` directories
- Scripts can be run from any directory (they auto-detect repo root)
- Windows scripts use PowerShell 5.1+ syntax
- Linux/macOS scripts are POSIX-compliant bash
