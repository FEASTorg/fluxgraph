# Changelog

All notable changes to FluxGraph will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2024-02-16

### Added

**Core Library:**
- `SignalStore` - Type-safe signal storage with unit metadata and physics-driven flags
- `SignalNamespace` - Path-to-ID interning for fast signal lookups
- `FunctionNamespace` - Function registration and lookup
- `DeviceId`, `SignalId` - Type-safe integer handles
- `Variant` - Runtime-typed variant supporting double, string, bool, int64
- `Command` - Typed command structure for device actions

**Transform System:**
- `ITransform` interface for stateful signal transforms
- 8 built-in transforms:
  - `LinearTransform` - Scale, offset, and clamping
  - `FirstOrderLagTransform` - Exponential smoothing with configurable time constant
  - `DelayTransform` - Time-delayed signal with circular buffer
  - `NoiseTransform` - Gaussian white noise with optional seed
  - `SaturationTransform` - Min/max clamping
  - `DeadbandTransform` - Threshold-based zeroing
  - `RateLimiterTransform` - Rate of change limiting
  - `MovingAverageTransform` - Sliding window average (FIR filter)

**Physics Models:**
- `IModel` interface with stability limits
- `ThermalMassModel` - Lumped capacitance heat equation (Forward Euler integration)

**Graph System:**
- `GraphSpec` - Protocol-agnostic POD structure for graph definition
- `GraphCompiler` - Topological sort, cycle detection, stability validation
- `Engine` - Five-stage deterministic tick execution:
  1. Snapshot inputs
  2. Process edges (topological order)
  3. Update models
  4. Commit outputs
  5. Evaluate rules

**Optional Loaders:**
- JSON graph loader (`load_json_file`, `load_json_string`)
  - Requires `-DFLUXGRAPH_JSON_ENABLED=ON`
  - Uses nlohmann/json v3.11.3 (header-only, PRIVATE linkage)
- YAML graph loader (`load_yaml_file`, `load_yaml_string`)
  - Requires `-DFLUXGRAPH_YAML_ENABLED=ON`
  - Uses yaml-cpp master branch (static lib, PRIVATE linkage)

**Testing:**
- 153 tests for core library (zero dependencies)
- 162 tests with JSON or YAML loader enabled
- 171 tests with both loaders enabled
- 19 analytical validation tests:
  - FirstOrderLag vs exp(-t/tau): 1e-3 tolerance
  - ThermalMass vs heat equation: 0.1°C tolerance
  - Delay: 1e-6 exact time-shift
  - Linear, Saturation, Deadband, RateLimiter, MovingAverage: exact validation

**Examples:**
- `01_basic_transform` - Simple linear transform
- `02_thermal_mass` - Physics simulation with thermal mass model
- `03_json_graph` - Load thermal chamber graph from JSON file
- `04_yaml_graph` - Load thermal chamber graph from YAML file

**Documentation:**
- API reference (`docs/API.md`)
- JSON schema documentation (`docs/JSON_SCHEMA.md`)
- YAML schema documentation (`docs/YAML_SCHEMA.md`)
- Embedding guide (`docs/EMBEDDING.md`)
- Transform reference (`docs/TRANSFORMS.md`)

**Build System:**
- CMake 3.20+ build system
- Conditional compilation for optional loaders
- FetchContent for automatic dependency management
- CMake export configuration (`cmake/fluxgraphConfig.cmake.in`)

### Changed
- N/A (initial release)

### Deprecated
- N/A (initial release)

### Removed
- N/A (initial release)

### Fixed
- N/A (initial release)

### Security
- N/A (initial release)

## Design Philosophy

FluxGraph follows these principles:

1. **Zero-dependency core** - Core library has no external dependencies
2. **Protocol-agnostic** - No assumptions about YAML, gRPC, or protobuf
3. **Single-writer design** - No internal synchronization overhead
4. **Embeddable** - Works in any C++ host application
5. **Scientific rigor** - Dimensional analysis, topological correctness, stability validation
6. **Modern C++** - C++17 with clean, idiomatic API

---

[1.0.0]: https://github.com/FEASTorg/fluxgraph/releases/tag/v1.0.0
