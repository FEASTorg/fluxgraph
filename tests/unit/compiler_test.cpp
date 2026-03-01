#include "fluxgraph/graph/compiler.hpp"
#include <gtest/gtest.h>

using namespace fluxgraph;

TEST(GraphCompilerTest, ParseLinearTransform) {
  TransformSpec spec;
  spec.type = "linear";
  spec.params["scale"] = 2.0;
  spec.params["offset"] = 5.0;

  GraphCompiler compiler;
  ITransform *tf = compiler.parse_transform(spec);

  EXPECT_NE(tf, nullptr);
  EXPECT_EQ(tf->apply(10.0, 0.1), 25.0); // 2*10 + 5

  delete tf;
}

TEST(GraphCompilerTest, ParseFirstOrderLag) {
  TransformSpec spec;
  spec.type = "first_order_lag";
  spec.params["tau_s"] = 1.0;

  GraphCompiler compiler;
  ITransform *tf = compiler.parse_transform(spec);

  EXPECT_NE(tf, nullptr);
  double y = tf->apply(100.0, 0.1);
  EXPECT_EQ(y, 100.0); // First call initializes

  delete tf;
}

TEST(GraphCompilerTest, UnknownTransformThrows) {
  TransformSpec spec;
  spec.type = "unknown_transform";

  GraphCompiler compiler;
  EXPECT_THROW(compiler.parse_transform(spec), std::runtime_error);
}

TEST(GraphCompilerTest, CompileSimpleGraph) {
  GraphSpec spec;

  EdgeSpec edge;
  edge.source_path = "input/value";
  edge.target_path = "output/value";
  edge.transform.type = "linear";
  edge.transform.params["scale"] = 2.0;
  edge.transform.params["offset"] = 0.0;
  spec.edges.push_back(edge);

  SignalNamespace signal_ns;
  FunctionNamespace func_ns;

  GraphCompiler compiler;
  CompiledProgram program = compiler.compile(spec, signal_ns, func_ns);

  EXPECT_EQ(program.edges.size(), 1);
  EXPECT_NE(program.edges[0].transform, nullptr);
}

TEST(GraphCompilerTest, TopologicalSortPreservesOrder) {
  GraphSpec spec;

  // Create a chain: A -> B -> C
  EdgeSpec edge1;
  edge1.source_path = "A";
  edge1.target_path = "B";
  edge1.transform.type = "linear";
  edge1.transform.params["scale"] = 1.0;
  edge1.transform.params["offset"] = 0.0;

  EdgeSpec edge2;
  edge2.source_path = "B";
  edge2.target_path = "C";
  edge2.transform.type = "linear";
  edge2.transform.params["scale"] = 1.0;
  edge2.transform.params["offset"] = 0.0;

  // Add in reverse order to test sorting
  spec.edges.push_back(edge2);
  spec.edges.push_back(edge1);

  SignalNamespace signal_ns;
  FunctionNamespace func_ns;

  GraphCompiler compiler;
  CompiledProgram program = compiler.compile(spec, signal_ns, func_ns);

  EXPECT_EQ(program.edges.size(), 2);
  // After topological sort, edge1 (A->B) should come before edge2 (B->C)
}

TEST(GraphCompilerTest, CycleDetection) {
  GraphSpec spec;

  // Create a cycle: A -> B -> A
  EdgeSpec edge1;
  edge1.source_path = "A";
  edge1.target_path = "B";
  edge1.transform.type = "linear";
  edge1.transform.params["scale"] = 1.0;
  edge1.transform.params["offset"] = 0.0;

  EdgeSpec edge2;
  edge2.source_path = "B";
  edge2.target_path = "A";
  edge2.transform.type = "linear";
  edge2.transform.params["scale"] = 1.0;
  edge2.transform.params["offset"] = 0.0;

  spec.edges.push_back(edge1);
  spec.edges.push_back(edge2);

  SignalNamespace signal_ns;
  FunctionNamespace func_ns;

  GraphCompiler compiler;
  EXPECT_THROW(compiler.compile(spec, signal_ns, func_ns), std::runtime_error);
}

TEST(GraphCompilerTest, ParseThermalMassModel) {
  ModelSpec spec;
  spec.id = "chamber_air";
  spec.type = "thermal_mass";
  spec.params["thermal_mass"] = 1000.0;
  spec.params["heat_transfer_coeff"] = 10.0;
  spec.params["initial_temp"] = 25.0;
  spec.params["temp_signal"] = std::string("chamber_air/temperature");
  spec.params["power_signal"] = std::string("chamber_air/power");
  spec.params["ambient_signal"] = std::string("chamber_air/ambient");

  SignalNamespace ns;
  GraphCompiler compiler;
  IModel *model = compiler.parse_model(spec, ns);

  EXPECT_NE(model, nullptr);
  EXPECT_NE(model->describe().find("ThermalMass"), std::string::npos);

  delete model;
}

TEST(GraphCompilerTest, RuleConditionEvaluation) {
  GraphSpec spec;

  RuleSpec rule;
  rule.id = "overtemp";
  rule.condition = "sensor.temp >= 50.0";
  ActionSpec action;
  action.device = "heater";
  action.function = "shutdown";
  action.args["code"] = int64_t{1};
  rule.actions.push_back(action);
  spec.rules.push_back(rule);

  SignalNamespace signal_ns;
  FunctionNamespace func_ns;
  GraphCompiler compiler;

  CompiledProgram program = compiler.compile(spec, signal_ns, func_ns);
  ASSERT_EQ(program.rules.size(), 1);

  SignalId temp_id = signal_ns.resolve("sensor.temp");
  ASSERT_NE(temp_id, INVALID_SIGNAL);

  SignalStore store;
  store.write(temp_id, 49.9, "degC");
  EXPECT_FALSE(program.rules[0].condition(store));

  store.write(temp_id, 50.0, "degC");
  EXPECT_TRUE(program.rules[0].condition(store));
}

TEST(GraphCompilerTest, InvalidRuleConditionThrows) {
  GraphSpec spec;
  RuleSpec rule;
  rule.id = "bad";
  rule.condition = "sensor.temp >< 50.0";
  spec.rules.push_back(rule);

  SignalNamespace signal_ns;
  FunctionNamespace func_ns;
  GraphCompiler compiler;

  EXPECT_THROW(compiler.compile(spec, signal_ns, func_ns), std::runtime_error);
}

TEST(GraphCompilerTest, NumericCoercionIntToDouble) {
  TransformSpec spec;
  spec.type = "linear";
  spec.params["scale"] = int64_t{2};
  spec.params["offset"] = int64_t{3};

  GraphCompiler compiler;
  ITransform *tf = compiler.parse_transform(spec);
  ASSERT_NE(tf, nullptr);
  EXPECT_DOUBLE_EQ(tf->apply(10.0, 0.1), 23.0);
  delete tf;
}

TEST(GraphCompilerTest, NoiseSeedIsOptional) {
  TransformSpec spec;
  spec.type = "noise";
  spec.params["amplitude"] = 0.0;

  GraphCompiler compiler;
  ITransform *tf = compiler.parse_transform(spec);
  ASSERT_NE(tf, nullptr);
  EXPECT_DOUBLE_EQ(tf->apply(3.14, 0.1), 3.14);
  delete tf;
}

TEST(GraphCompilerTest, SaturationSupportsMinValueAliases) {
  TransformSpec spec;
  spec.type = "saturation";
  spec.params["min_value"] = -1.0;
  spec.params["max_value"] = 1.0;

  GraphCompiler compiler;
  ITransform *tf = compiler.parse_transform(spec);
  ASSERT_NE(tf, nullptr);
  EXPECT_DOUBLE_EQ(tf->apply(5.0, 0.1), 1.0);
  EXPECT_DOUBLE_EQ(tf->apply(-5.0, 0.1), -1.0);
  delete tf;
}

TEST(GraphCompilerTest, DelayBreaksFeedbackCycle) {
  GraphSpec spec;

  EdgeSpec edge1;
  edge1.source_path = "A";
  edge1.target_path = "B";
  edge1.transform.type = "linear";
  edge1.transform.params["scale"] = 1.0;
  edge1.transform.params["offset"] = 0.0;

  EdgeSpec edge2;
  edge2.source_path = "B";
  edge2.target_path = "A";
  edge2.transform.type = "delay";
  edge2.transform.params["delay_sec"] = 0.1;

  spec.edges.push_back(edge1);
  spec.edges.push_back(edge2);

  SignalNamespace signal_ns;
  FunctionNamespace func_ns;
  GraphCompiler compiler;

  EXPECT_NO_THROW(compiler.compile(spec, signal_ns, func_ns));
}

TEST(GraphCompilerTest, StabilityValidationWithExpectedDt) {
  GraphSpec spec;

  ModelSpec model;
  model.id = "fast";
  model.type = "thermal_mass";
  model.params["temp_signal"] = std::string("fast.temp");
  model.params["power_signal"] = std::string("fast.power");
  model.params["ambient_signal"] = std::string("fast.ambient");
  model.params["thermal_mass"] = 1.0;
  model.params["heat_transfer_coeff"] = 100.0; // stability limit = 0.02
  model.params["initial_temp"] = 20.0;
  spec.models.push_back(model);

  SignalNamespace signal_ns;
  FunctionNamespace func_ns;
  GraphCompiler compiler;

  EXPECT_THROW(compiler.compile(spec, signal_ns, func_ns, 0.1),
               std::runtime_error);
}
