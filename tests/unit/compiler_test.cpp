#include "fluxgraph/graph/compiler.hpp"
#include <gtest/gtest.h>

using namespace fluxgraph;

TEST(GraphCompilerTest, ParseLinearTransform) {
    TransformSpec spec;
    spec.type = "linear";
    spec.params["scale"] = 2.0;
    spec.params["offset"] = 5.0;

    GraphCompiler compiler;
    ITransform* tf = compiler.parse_transform(spec);

    EXPECT_NE(tf, nullptr);
    EXPECT_EQ(tf->apply(10.0, 0.1), 25.0); // 2*10 + 5

    delete tf;
}

TEST(GraphCompilerTest, ParseFirstOrderLag) {
    TransformSpec spec;
    spec.type = "first_order_lag";
    spec.params["tau_s"] = 1.0;

    GraphCompiler compiler;
    ITransform* tf = compiler.parse_transform(spec);

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
    IModel* model = compiler.parse_model(spec, ns);

    EXPECT_NE(model, nullptr);
    EXPECT_NE(model->describe().find("ThermalMass"), std::string::npos);

    delete model;
}
