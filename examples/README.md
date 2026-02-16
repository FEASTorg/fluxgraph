# FluxGraph Examples

This directory contains usage examples demonstrating the FluxGraph library API, progressing from simple manual graph composition to more complex physics simulations.

## Building Examples

Examples are built automatically when `FLUXGRAPH_BUILD_EXAMPLES=ON` (default):

```bash
cmake -B build
cmake --build build --config Debug
```

## Example 1: Basic Transform

**Location:** `01_basic_transform/`

Demonstrates the fundamental FluxGraph API pattern:
- Manual `GraphSpec` construction (no YAML)
- Simple signal edge with `LinearTransform` (y = 2x + 1)
- Input/output "ports" as `SignalId` handles
- Basic simulation loop: `write()` -> `tick()` -> `read_value()`

**Run:**
```bash
./build/examples/01_basic_transform/Debug/example_basic_transform.exe
```

**Expected Output:**
```
Simple Transform: y = 2*x + 1
================================
Input: 0V -> Output: 1V
Input: 1V -> Output: 3V
Input: 2V -> Output: 5V
Input: 3V -> Output: 7V
Input: 4V -> Output: 9V
```

**Key API Concepts:**
- `GraphSpec` - Protocol-agnostic POD graph definition
- `GraphCompiler::compile()` - Validates and optimizes graph
- `Engine::load()` - Sets up execution state
- `SignalNamespace::resolve()` - Gets signal IDs from paths
- `SignalStore::write()` and `read_value()` - Signal I/O

## Example 2: Thermal Mass Simulation

**Location:** `02_thermal_mass/`

Shows realistic physics simulation with:
- `ThermalMassModel` - First-order thermal system
- Multiple input ports (heater power, ambient temperature)
- Stateful simulation with noise transform
- Heating and cooling phases

**Run:**
```bash
./build/examples/02_thermal_mass/Debug/example_thermal_mass.exe
```

**Expected Output:**
```
Thermal Mass Simulation
=======================
t= 0.00s  Heater=500.00W  Temp= 25.23 degC  Noisy= 25.15 degC
t= 0.50s  Heater=500.00W  Temp= 25.45 degC  Noisy= 25.38 degC
...
t= 5.00s  Heater=  0.00W  Temp= 27.42 degC  Noisy= 27.48 degC
t= 5.50s  Heater=  0.00W  Temp= 27.38 degC  Noisy= 27.31 degC
...
```

**Physics:** 
- Thermal mass: C = 1000 J/K
- Heat transfer: h = 10 W/K  
- Time constant: tau = C/h = 100 seconds
- Heating: 500W -> steady-state delta_T = 50 degC above ambient

**Key API Concepts:**
- `ModelSpec` - Physics model configuration
- Model input/output signals (power_in, temperature_out)
- Transform chains (physics -> noise filter)
- Timestep management (dt parameter in `tick()`)

## Example 3: YAML Configuration (Optional)

**Location:** `03_yaml_config/` *(requires yaml-cpp dependency)*

Shows the optional YAML config layer for complex graphs. Same execution API, just different graph construction method.

See Phase 23 plan for details on YAML parser implementation.

## When to Use Each Approach

### Manual GraphSpec (Examples 1 & 2)
**Use when:**
- Embedding FluxGraph in existing code
- Generating graphs programmatically
- No external config file needed
- Dynamic graph construction at runtime

**Benefits:**
- Zero external dependencies (core library only)
- Type-safe at compile time
- Full programmatic control
- No parsing overhead

### YAML Configuration (Example 3)
**Use when:**
- Complex graphs with many edges/models
- Non-programmers need to edit configs
- Shared configs across multiple tools
- Phase 22 compatibility required

**Benefits:**
- Human-readable/editable
- Declarative syntax
- Separation of config and code
- Version control friendly

## Next Steps

After understanding these examples:

1. **Explore transforms** - See `include/fluxgraph/transform/` for all 8 types
2. **Add models** - Implement custom physics models via `IModel` interface
3. **Check tests** - `tests/unit/` and `tests/analytical/` show comprehensive usage
4. **Read docs** - See `docs/` for architecture and design decisions

## API Quick Reference

```cpp
// 1. Setup
SignalNamespace sig_ns;
SignalStore store;
Engine engine;

// 2. Build graph (manual or from YAML)
GraphSpec spec;
spec.edges.push_back({source, target, transform});
spec.models.push_back({id, type, params});

// 3. Compile and load
GraphCompiler compiler;
auto program = compiler.compile(spec, sig_ns, func_ns);
engine.load(std::move(program));

// 4. Get ports
auto input_sig = sig_ns.resolve("device.signal_name");
auto output_sig = sig_ns.resolve("other.output");

// 5. Simulation loop
store.write(input_sig, value, "unit");
engine.tick(dt, store);
double result = store.read_value(output_sig);
```

## Troubleshooting

**"Unknown model type" error:**
- Check ModelSpec `type` field matches implemented model ("thermal_mass")
- Ensure all required params are present

**Signals read as 0.0:**
- Verify signal was written before reading
- Check SignalNamespace path spelling
- Confirm `tick()` was called to propagate changes

**Unexpected NaN/Inf values:**
- Check model stability limits (dt too large)
- Verify thermal_mass and heat_transfer_coeff > 0
- Ensure ambient temperature is initialized

**Compile errors:**
- Use `target_path` not `dest_path` in EdgeSpec
- Use `temp_signal` not `temperature_signal` for ThermalMassModel params
- Include all required headers (engine.hpp, compiler.hpp, etc.)
