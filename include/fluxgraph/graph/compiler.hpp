#pragma once

#include "fluxgraph/core/namespace.hpp"
#include "fluxgraph/graph/spec.hpp"
#include "fluxgraph/model/interface.hpp"
#include "fluxgraph/transform/interface.hpp"
#include <functional>
#include <memory>
#include <vector>

namespace fluxgraph {

/// Compiled edge with resolved signal IDs and instantiated transform
struct CompiledEdge {
  SignalId source;
  SignalId target;
  std::unique_ptr<ITransform> transform;
  double snapshot; // Cached source value at tick start

  CompiledEdge(SignalId src, SignalId tgt, ITransform *tf)
      : source(src), target(tgt), transform(tf), snapshot(0.0) {}
};

/// Compiled rule with condition evaluator
struct CompiledRule {
  std::string id;
  std::function<bool(const SignalStore &)> condition;
  std::vector<std::pair<DeviceId, FunctionId>> device_functions;
  std::vector<std::map<std::string, Variant>> args_list;
  std::string on_error;
};

/// Compiled program ready for execution
struct CompiledProgram {
  std::vector<CompiledEdge> edges;
  std::vector<std::unique_ptr<IModel>> models;
  std::vector<CompiledRule> rules;
};

/// Compiles GraphSpec into executable CompiledProgram
class GraphCompiler {
public:
  GraphCompiler();
  ~GraphCompiler();

  /// Compile a graph specification
  /// @param spec Graph specification (POD)
  /// @param signal_ns Signal namespace for interning paths
  /// @param func_ns Function namespace for device/function IDs
  /// @return Compiled program ready for execution
  /// @throws std::runtime_error on compilation errors
  CompiledProgram compile(const GraphSpec &spec, SignalNamespace &signal_ns,
                          FunctionNamespace &func_ns);

  // Public for testing
  ITransform *parse_transform(const TransformSpec &spec);
  IModel *parse_model(const ModelSpec &spec, SignalNamespace &ns);

private:
  // Scientific rigor: Graph validation
  void topological_sort(std::vector<CompiledEdge> &edges);
  void detect_cycles(const std::vector<CompiledEdge> &edges);
  void validate_stability(const std::vector<std::unique_ptr<IModel>> &models,
                          double expected_dt);
};

} // namespace fluxgraph
