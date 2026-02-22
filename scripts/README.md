# FluxGraph Scripts (Preset-First)

These scripts are thin wrappers around CMake presets.

## Prerequisite

Set `VCPKG_ROOT` so presets can resolve the toolchain:

- Linux/macOS: `export VCPKG_ROOT=/path/to/vcpkg`
- Windows PowerShell: `$env:VCPKG_ROOT = "D:\\Tools\\vcpkg"`

## Build

- Linux/macOS: `bash ./scripts/build.sh --preset dev-release`
- Windows: `.\scripts\build.ps1 -Preset dev-windows-release`

Options:

- `--preset` / `-Preset`: configure+build preset name.
- `--clean-first` / `-CleanFirst`: clean target before build.
- `-j/--jobs` / `-Jobs`: build parallelism.

## Test

- Linux/macOS: `bash ./scripts/test.sh --preset dev-release`
- Windows: `.\scripts\test.ps1 -Preset dev-windows-release`

Options:

- `--preset` / `-Preset`: CTest preset name.
- `--verbose` / `-Verbose`: verbose test output.

## Common Presets

- `dev-debug`
- `dev-release`
- `dev-windows-debug`
- `dev-windows-release`
- `ci-linux-release`
- `ci-linux-release-json`
- `ci-linux-release-yaml`
- `ci-linux-release-server`
- `ci-windows-release`

## Server + Python gRPC Integration (CI-aligned)

1. Configure/build with `ci-linux-release-server`.
2. Generate Python protobuf bindings:
   - `bash ./scripts/generate_proto_python.sh`
3. Start server binary from `build-server`.
4. Run: `python3 tests/test_grpc_integration.py`
