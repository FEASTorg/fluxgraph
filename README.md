# FluxGraph

**A protocol-agnostic physics simulation library for embedded systems**

FluxGraph is a standalone C++ library that provides signal storage, graph compilation, transforms, models, and deterministic tick execution. Extracted from the FEAST Anolis provider architecture, FluxGraph enables embeddable physics simulation in any C++ host application.

## Features

- **Zero dependencies** - Core library has no external dependencies
- **Protocol-agnostic** - No assumptions about YAML, gRPC, or protobuf in core
- **Single-writer design** - No internal synchronization overhead
- **Embeddable** - Works in any C++ host application
- **Scientific rigor** - Dimensional analysis, topological correctness, stability validation
- **C++17** - Modern C++ with clean API

## Quick Start

### Prerequisites

- CMake 3.20 or later
- C++17 compatible compiler (MSVC 2019+, GCC 9+, Clang 10+)

### Building

```bash
# Clone and configure
git clone https://github.com/FEASTorg/fluxgraph.git
cd fluxgraph
cmake -B build -S .

# Build
cmake --build build

# Run tests
cd build
ctest --output-on-failure
```

### Using FluxGraph

See [`examples/`](examples/) for complete usage patterns. Here's a minimal example:

```cpp
#include <fluxgraph/engine.hpp>
#include <fluxgraph/core/signal_store.hpp>
#include <fluxgraph/core/namespace.hpp>
#include <fluxgraph/graph/compiler.hpp>

int main() {
    // 1. Create namespaces and signal store
    fluxgraph::SignalNamespace sig_ns;
    fluxgraph::FunctionNamespace func_ns;
    fluxgraph::SignalStore store;

    // 2. Build graph specification
    fluxgraph::GraphSpec spec;
    fluxgraph::EdgeSpec edge;
    edge.source_path = "sensor.voltage_in";
    edge.target_path = "sensor.voltage_out";
    edge.transform.type = "linear";
    edge.transform.params["scale"] = 2.0;
    edge.transform.params["offset"] = 1.0;
    spec.edges.push_back(edge);

    // 3. Compile and load
    fluxgraph::GraphCompiler compiler;
    auto program = compiler.compile(spec, sig_ns, func_ns);
    fluxgraph::Engine engine;
    engine.load(std::move(program));

    // 4. Get signal ports
    auto input_sig = sig_ns.resolve("sensor.voltage_in");
    auto output_sig = sig_ns.resolve("sensor.voltage_out");

    // 5. Simulation loop
    store.write(input_sig, 5.0, "V");
    engine.tick(0.1, store);
    double result = store.read_value(output_sig);  // 11.0V (2*5 + 1)
    
    return 0;
}
```

**More examples:**
- [`01_basic_transform`](examples/01_basic_transform/) - Simple signal transformation
- [`02_thermal_mass`](examples/02_thermal_mass/) - Physics simulation with models

## Project Structure

```
fluxgraph/
+-- include/fluxgraph/     # Public API headers
|   +-- core/              # Core types, signal storage, namespaces
|   +-- transform/         # Signal transforms
|   +-- model/             # Physics models
+-- src/                   # Implementation
+-- tests/                 # Unit and analytical tests
+-- examples/              # Example applications
+-- docs/                  # Documentation
```

## Development Status

**Phase 23: Core Library - Week 5 Complete**

- [x] Core types (`SignalId`, `DeviceId`, `Variant`)
- [x] `SignalStore` with unit metadata and physics-driven flags
- [x] `SignalNamespace` for path interning
- [x] `Command` structure with typed arguments
- [x] Transform interface (`ITransform`)
- [x] 8 Transform implementations:
  - `LinearTransform` - Scale and offset with clamping
  - `FirstOrderLagTransform` - Exponential smoothing (tau_s)
  - `DelayTransform` - Time delay with ring buffer (delay_sec)
  - `NoiseTransform` - Gaussian noise (amplitude, deterministic seed)
  - `SaturationTransform` - Min/max clamping
  - `DeadbandTransform` - Threshold-based zeroing
  - `RateLimiterTransform` - Rate of change limiting (max_rate_per_sec)
  - `MovingAverageTransform` - Sliding window average
- [x] Model interface (`IModel`) with stability limits
- [x] `ThermalMassModel` - Heat equation physics (Forward Euler)
- [x] `GraphSpec` - Protocol-agnostic POD structures
- [x] `GraphCompiler` - Topological sort, cycle detection, stability validation
- [x] `Engine` - Five-stage tick execution:
  1. Snapshot inputs
  2. Process edges (in topological order)
  3. Update models
  4. Commit outputs
  5. Evaluate rules
- [x] **147 tests passing** (128 unit tests + 19 analytical tests)
- [x] **Analytical validation suite** - Validates numerical accuracy vs closed-form solutions:
  - FirstOrderLag: 1e-3 tolerance vs exp(-t/tau)
  - ThermalMass: 0.1 degC vs heat equation
  - Delay: 1e-6 exact time-shift
  - Linear, Saturation, Deadband, RateLimiter, MovingAverage: exact validation
- [x] **Usage examples** - Manual graph composition and physics simulation
- [ ] YAML config parser (optional, requires yaml-cpp)

## License

AGPL v3 License - See [LICENSE](LICENSE) for details

## Contributing

This project is part of the FEAST ecosystem. See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## Related Projects

- [anolis](https://github.com/FEASTorg/anolis) - FEAST automation runtime
- [anolis-provider-sim](https://github.com/FEASTorg/anolis-provider-sim) - Simulation provider

## Version

0.1.0 - Initial development
