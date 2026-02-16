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

```cpp
#include <fluxgraph/core/signal_store.hpp>
#include <fluxgraph/core/namespace.hpp>

// Create signal storage and namespace
fluxgraph::SignalStore store;
fluxgraph::SignalNamespace ns;

// Intern signal paths
auto temp_id = ns.intern("chamber/temperature");
auto setpoint_id = ns.intern("chamber/setpoint");

// Write signals with units
store.write(temp_id, 25.0, "degC");
store.write(setpoint_id, 100.0, "degC");

// Read signals
auto temp = store.read(temp_id);
std::cout << "Temperature: " << temp.value << " " << temp.unit << "\n";
```

## Project Structure

```
fluxgraph/
├── include/fluxgraph/     # Public API headers
│   ├── core/              # Core types, signal storage, namespaces
│   ├── transform/         # Signal transforms (planned)
│   └── model/             # Physics models (planned)
├── src/                   # Implementation
├── tests/                 # Unit and analytical tests
├── examples/              # Example applications (planned)
└── docs/                  # Documentation (planned)
```

## Development Status

**Phase 23: Core Library - Week 1** (Current)

- ✅ Core types (`SignalId`, `DeviceId`, `Variant`)
- ✅ `SignalStore` with unit metadata
- ✅ `SignalNamespace` for path interning
- ✅ `Command` structure
- ✅ Unit tests (35+ tests)
- 🚧 Transforms (Week 2)
- 🚧 Models (Week 3-4)
- 🚧 Graph compiler (Week 3-4)
- 🚧 Engine (Week 3-4)

## License

MIT License - See [LICENSE](LICENSE) for details

## Contributing

This project is part of the FEAST ecosystem. See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## Related Projects

- [anolis](https://github.com/FEASTorg/anolis) - FEAST automation runtime
- [anolis-provider-sim](https://github.com/FEASTorg/anolis-provider-sim) - Simulation provider

## Version

0.1.0 - Initial development
