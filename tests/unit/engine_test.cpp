#include "fluxgraph/engine.hpp"
#include <gtest/gtest.h>

using namespace fluxgraph;

TEST(EngineTest, LoadProgram) {
    Engine engine;
    EXPECT_FALSE(engine.is_loaded());

    CompiledProgram program;
    engine.load(std::move(program));

    EXPECT_TRUE(engine.is_loaded());
}

TEST(EngineTest, TickRequiresLoadedProgram) {
    Engine engine;
    SignalStore store;

    EXPECT_THROW(engine.tick(0.1, store), std::runtime_error);
}

TEST(EngineTest, SimpleEdgeExecution) {
    GraphSpec spec;

    EdgeSpec edge;
    edge.source_path = "input";
    edge.target_path = "output";
    edge.transform.type = "linear";
    edge.transform.params["scale"] = 2.0;
    edge.transform.params["offset"] = 0.0;
    spec.edges.push_back(edge);

    SignalNamespace signal_ns;
    FunctionNamespace func_ns;
    SignalStore store;

    GraphCompiler compiler;
    CompiledProgram program = compiler.compile(spec, signal_ns, func_ns);

    Engine engine;
    engine.load(std::move(program));

    // Set input
    SignalId input_id = signal_ns.resolve("input");
    SignalId output_id = signal_ns.resolve("output");
    store.write(input_id, 10.0, "dimensionless");

    // Tick
    engine.tick(0.1, store);

    // Check output
    double output = store.read_value(output_id);
    EXPECT_EQ(output, 20.0); // 2 * 10
}

TEST(EngineTest, DrainCommands) {
    Engine engine;
    CompiledProgram program;
    engine.load(std::move(program));

    auto commands = engine.drain_commands();
    EXPECT_TRUE(commands.empty());

    // Drain again should still be empty
    commands = engine.drain_commands();
    EXPECT_TRUE(commands.empty());
}

TEST(EngineTest, Reset) {
    GraphSpec spec;

    EdgeSpec edge;
    edge.source_path = "input";
    edge.target_path = "output";
    edge.transform.type = "first_order_lag";
    edge.transform.params["tau_s"] = 1.0;
    spec.edges.push_back(edge);

    SignalNamespace signal_ns;
    FunctionNamespace func_ns;
    SignalStore store;

    GraphCompiler compiler;
    CompiledProgram program = compiler.compile(spec, signal_ns, func_ns);

    Engine engine;
    engine.load(std::move(program));

    SignalId input_id = signal_ns.resolve("input");
    SignalId output_id = signal_ns.resolve("output");

    // Run some ticks
    store.write(input_id, 100.0, "dimensionless");
    for (int i = 0; i < 10; ++i) {
        engine.tick(0.1, store);
    }

    double output_before_reset = store.read_value(output_id);

    // Reset
    engine.reset();

    // Tick again with different input
    store.write(input_id, 50.0, "dimensionless");
    engine.tick(0.1, store);

    double output_after_reset = store.read_value(output_id);

    // After reset, output should reinitialize to new input
    EXPECT_EQ(output_after_reset, 50.0);
}

TEST(EngineTest, ThermalMassIntegration) {
    GraphSpec spec;

    ModelSpec model_spec;
    model_spec.id = "thermal_test";
    model_spec.type = "thermal_mass";
    model_spec.params["thermal_mass"] = 1000.0;
    model_spec.params["heat_transfer_coeff"] = 10.0;
    model_spec.params["initial_temp"] = 25.0;
    model_spec.params["temp_signal"] = std::string("model/temperature");
    model_spec.params["power_signal"] = std::string("model/power");
    model_spec.params["ambient_signal"] = std::string("model/ambient");
    spec.models.push_back(model_spec);

    SignalNamespace signal_ns;
    FunctionNamespace func_ns;
    SignalStore store;

    GraphCompiler compiler;
    CompiledProgram program = compiler.compile(spec, signal_ns, func_ns);

    Engine engine;
    engine.load(std::move(program));

    // Set up inputs
    SignalId power_id = signal_ns.resolve("model/power");
    SignalId ambient_id = signal_ns.resolve("model/ambient");
    SignalId temp_id = signal_ns.resolve("model/temperature");

    store.write(power_id, 100.0, "W");
    store.write(ambient_id, 20.0, "degC");

    // Run simulation
    for (int i = 0; i < 10; ++i) {
        engine.tick(0.1, store);
    }

    double final_temp = store.read_value(temp_id);
    EXPECT_GT(final_temp, 25.0); // Should have heated up
}
