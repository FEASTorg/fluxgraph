#include "fluxgraph/core/namespace.hpp"
#include "fluxgraph/core/signal_store.hpp"
#include "fluxgraph/engine.hpp"
#include "fluxgraph/graph/compiler.hpp"
#include <cmath>
#include <gtest/gtest.h>

using namespace fluxgraph;

TEST(DeterminismTest, SameInputSameOutput) {
  // Verify that identical input sequences produce identical outputs

  auto build_graph = []() {
    GraphSpec spec;

    // Add model
    ModelSpec model;
    model.id = "thermal";
    model.type = "thermal_mass";
    model.params["temp_signal"] = std::string("chamber.temp");
    model.params["power_signal"] = std::string("chamber.power");
    model.params["ambient_signal"] = std::string("ambient");
    model.params["thermal_mass"] = 1000.0;
    model.params["heat_transfer_coeff"] = 10.0;
    model.params["initial_temp"] = 25.0;
    spec.models.push_back(model);

    // Add transform
    EdgeSpec edge;
    edge.source_path = "chamber.temp";
    edge.target_path = "chamber.temp_filtered";
    edge.transform.type = "first_order_lag";
    edge.transform.params["tau_s"] = 1.0;
    spec.edges.push_back(edge);

    return spec;
  };

  // Run 1: First execution
  SignalNamespace ns1;
  FunctionNamespace fn1;
  SignalStore store1;
  Engine engine1;

  GraphCompiler compiler1;
  auto program1 = compiler1.compile(build_graph(), ns1, fn1);
  engine1.load(std::move(program1));

  auto power_id1 = ns1.resolve("chamber.power");
  auto ambient_id1 = ns1.resolve("ambient");
  auto temp_id1 = ns1.resolve("chamber.temp");

  store1.write(ambient_id1, 20.0, "degC");

  std::vector<double> temps1;
  for (int i = 0; i < 1000; ++i) {
    double power = (i < 500) ? 500.0 : 0.0;
    store1.write(power_id1, power, "W");
    engine1.tick(0.1, store1);
    temps1.push_back(store1.read_value(temp_id1));
  }

  // Run 2: Second execution with identical inputs
  SignalNamespace ns2;
  FunctionNamespace fn2;
  SignalStore store2;
  Engine engine2;

  GraphCompiler compiler2;
  auto program2 = compiler2.compile(build_graph(), ns2, fn2);
  engine2.load(std::move(program2));

  auto power_id2 = ns2.resolve("chamber.power");
  auto ambient_id2 = ns2.resolve("ambient");
  auto temp_id2 = ns2.resolve("chamber.temp");

  store2.write(ambient_id2, 20.0, "degC");

  std::vector<double> temps2;
  for (int i = 0; i < 1000; ++i) {
    double power = (i < 500) ? 500.0 : 0.0;
    store2.write(power_id2, power, "W");
    engine2.tick(0.1, store2);
    temps2.push_back(store2.read_value(temp_id2));
  }

  // Verify bit-exact determinism
  ASSERT_EQ(temps1.size(), temps2.size());
  for (size_t i = 0; i < temps1.size(); ++i) {
    EXPECT_DOUBLE_EQ(temps1[i], temps2[i]) << "Mismatch at tick " << i;
  }
}

TEST(DeterminismTest, NoDriftOver10kTicks) {
  // Verify no floating-point drift over long simulations

  SignalNamespace ns;
  FunctionNamespace fn;
  SignalStore store;
  Engine engine;

  GraphSpec spec;
  ModelSpec model;
  model.id = "thermal";
  model.type = "thermal_mass";
  model.params["temp_signal"] = std::string("chamber.temp");
  model.params["power_signal"] = std::string("chamber.power");
  model.params["ambient_signal"] = std::string("ambient");
  model.params["thermal_mass"] = 1000.0;
  model.params["heat_transfer_coeff"] = 10.0;
  model.params["initial_temp"] = 25.0;
  spec.models.push_back(model);

  GraphCompiler compiler;
  auto program = compiler.compile(spec, ns, fn);
  engine.load(std::move(program));

  auto power_id = ns.resolve("chamber.power");
  auto ambient_id = ns.resolve("ambient");
  auto temp_id = ns.resolve("chamber.temp");

  store.write(ambient_id, 20.0, "degC");
  store.write(power_id, 50.0, "W"); // Equilibrium at 25 degC

  // Run to equilibrium
  for (int i = 0; i < 1000; ++i) {
    engine.tick(0.1, store);
  }

  double temp_baseline = store.read_value(temp_id);
  EXPECT_NEAR(temp_baseline, 25.0, 0.1);

  // Continue for 10k more ticks at equilibrium
  for (int i = 0; i < 10000; ++i) {
    engine.tick(0.1, store);
  }

  double temp_final = store.read_value(temp_id);

  // Should not drift from equilibrium
  EXPECT_NEAR(temp_final, temp_baseline, 0.01)
      << "Temperature drifted over 10k ticks";
}

TEST(DeterminismTest, ResetRestoresInitialState) {
  // Verify reset() fully restores initial conditions

  SignalNamespace ns;
  FunctionNamespace fn;
  SignalStore store;
  Engine engine;

  GraphSpec spec;

  // Add model
  ModelSpec model;
  model.id = "thermal";
  model.type = "thermal_mass";
  model.params["temp_signal"] = std::string("chamber.temp");
  model.params["power_signal"] = std::string("chamber.power");
  model.params["ambient_signal"] = std::string("ambient");
  model.params["thermal_mass"] = 1000.0;
  model.params["heat_transfer_coeff"] = 10.0;
  model.params["initial_temp"] = 25.0;
  spec.models.push_back(model);

  // Add stateful transform
  EdgeSpec edge;
  edge.source_path = "chamber.temp";
  edge.target_path = "chamber.temp_delayed";
  edge.transform.type = "delay";
  edge.transform.params["delay_sec"] = 1.0;
  spec.edges.push_back(edge);

  GraphCompiler compiler;
  auto program = compiler.compile(spec, ns, fn);
  engine.load(std::move(program));

  auto power_id = ns.resolve("chamber.power");
  auto ambient_id = ns.resolve("ambient");
  auto temp_id = ns.resolve("chamber.temp");

  store.write(ambient_id, 20.0, "degC");

  // Initial state
  store.write(power_id, 0.0, "W");
  engine.tick(0.1, store);
  double temp_initial = store.read_value(temp_id);

  // Run simulation
  for (int i = 0; i < 100; ++i) {
    store.write(power_id, 1000.0, "W");
    engine.tick(0.1, store);
  }
  double temp_after_heating = store.read_value(temp_id);
  EXPECT_GT(temp_after_heating, temp_initial + 5.0);

  // Reset
  engine.reset();
  store.clear(); // Also clear signal store

  // Verify back to initial state
  store.write(ambient_id, 20.0, "degC");
  store.write(power_id, 0.0, "W");
  engine.tick(0.1, store);
  double temp_after_reset = store.read_value(temp_id);

  EXPECT_DOUBLE_EQ(temp_after_reset, temp_initial);
}
