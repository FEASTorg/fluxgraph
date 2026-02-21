#include "fluxgraph/graph/compiler.hpp"
#include "fluxgraph/model/thermal_mass.hpp"
#include "fluxgraph/transform/deadband.hpp"
#include "fluxgraph/transform/delay.hpp"
#include "fluxgraph/transform/first_order_lag.hpp"
#include "fluxgraph/transform/linear.hpp"
#include "fluxgraph/transform/moving_average.hpp"
#include "fluxgraph/transform/noise.hpp"
#include "fluxgraph/transform/rate_limiter.hpp"
#include "fluxgraph/transform/saturation.hpp"
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <limits>

namespace fluxgraph {

GraphCompiler::GraphCompiler() = default;
GraphCompiler::~GraphCompiler() = default;

CompiledProgram GraphCompiler::compile(const GraphSpec &spec,
                                       SignalNamespace &signal_ns,
                                       FunctionNamespace &func_ns) {
  CompiledProgram program;

  // Compile models
  for (const auto &model_spec : spec.models) {
    auto *model = parse_model(model_spec, signal_ns);
    program.models.emplace_back(model);
  }

  // Compile edges
  for (const auto &edge_spec : spec.edges) {
    SignalId src = signal_ns.intern(edge_spec.source_path);
    SignalId tgt = signal_ns.intern(edge_spec.target_path);
    ITransform *tf = parse_transform(edge_spec.transform);
    program.edges.emplace_back(src, tgt, tf);
  }

  // Topological sort edges
  topological_sort(program.edges);

  // Detect cycles (algebraic loops)
  detect_cycles(program.edges);

  // Compile rules (simplified for now)
  for (const auto &rule_spec : spec.rules) {
    CompiledRule rule;
    rule.id = rule_spec.id;
    rule.on_error = rule_spec.on_error;

    // For now, create a dummy condition (will implement proper parser later)
    rule.condition = [](const SignalStore &) { return false; };

    for (const auto &action : rule_spec.actions) {
      DeviceId dev_id = func_ns.intern_device(action.device);
      FunctionId func_id = func_ns.intern_function(action.function);
      rule.device_functions.emplace_back(dev_id, func_id);
      rule.args_list.push_back(action.args);
    }

    program.rules.push_back(std::move(rule));
  }

  return program;
}

ITransform *GraphCompiler::parse_transform(const TransformSpec &spec) {
  const std::string &type = spec.type;

  if (type == "linear") {
    double scale = std::get<double>(spec.params.at("scale"));
    double offset = std::get<double>(spec.params.at("offset"));
    double clamp_min = -std::numeric_limits<double>::infinity();
    double clamp_max = std::numeric_limits<double>::infinity();

    if (spec.params.count("clamp_min")) {
      clamp_min = std::get<double>(spec.params.at("clamp_min"));
    }
    if (spec.params.count("clamp_max")) {
      clamp_max = std::get<double>(spec.params.at("clamp_max"));
    }

    return new LinearTransform(scale, offset, clamp_min, clamp_max);
  } else if (type == "first_order_lag") {
    double tau_s = std::get<double>(spec.params.at("tau_s"));
    return new FirstOrderLagTransform(tau_s);
  } else if (type == "delay") {
    double delay_sec = std::get<double>(spec.params.at("delay_sec"));
    return new DelayTransform(delay_sec);
  } else if (type == "noise") {
    double amplitude = std::get<double>(spec.params.at("amplitude"));
    uint32_t seed =
        static_cast<uint32_t>(std::get<int64_t>(spec.params.at("seed")));
    return new NoiseTransform(amplitude, seed);
  } else if (type == "saturation") {
    double min_val = std::get<double>(spec.params.at("min"));
    double max_val = std::get<double>(spec.params.at("max"));
    return new SaturationTransform(min_val, max_val);
  } else if (type == "deadband") {
    double threshold = std::get<double>(spec.params.at("threshold"));
    return new DeadbandTransform(threshold);
  } else if (type == "rate_limiter") {
    double max_rate = std::get<double>(spec.params.at("max_rate_per_sec"));
    return new RateLimiterTransform(max_rate);
  } else if (type == "moving_average") {
    size_t window_size =
        static_cast<size_t>(std::get<int64_t>(spec.params.at("window_size")));
    return new MovingAverageTransform(window_size);
  } else {
    throw std::runtime_error("Unknown transform type: " + type);
  }
}

IModel *GraphCompiler::parse_model(const ModelSpec &spec, SignalNamespace &ns) {
  const std::string &type = spec.type;

  if (type == "thermal_mass") {
    double thermal_mass = std::get<double>(spec.params.at("thermal_mass"));
    double heat_transfer_coeff =
        std::get<double>(spec.params.at("heat_transfer_coeff"));
    double initial_temp = std::get<double>(spec.params.at("initial_temp"));
    std::string temp_path =
        std::get<std::string>(spec.params.at("temp_signal"));
    std::string power_path =
        std::get<std::string>(spec.params.at("power_signal"));
    std::string ambient_path =
        std::get<std::string>(spec.params.at("ambient_signal"));

    return new ThermalMassModel(spec.id, thermal_mass, heat_transfer_coeff,
                                initial_temp, temp_path, power_path,
                                ambient_path, ns);
  } else {
    throw std::runtime_error("Unknown model type: " + type);
  }
}

void GraphCompiler::topological_sort(std::vector<CompiledEdge> &edges) {
  // Simplified topological sort using Kahn's algorithm
  // Build adjacency list and in-degree count
  std::map<SignalId, std::vector<size_t>> outgoing; // signal -> edge indices
  std::map<SignalId, int> in_degree;
  std::set<SignalId> all_signals;

  for (size_t i = 0; i < edges.size(); ++i) {
    all_signals.insert(edges[i].source);
    all_signals.insert(edges[i].target);
    outgoing[edges[i].source].push_back(i);
    in_degree[edges[i].target]++;
  }

  // Initialize queue with signals that have no incoming edges
  std::queue<SignalId> ready;
  for (SignalId sig : all_signals) {
    if (in_degree[sig] == 0) {
      ready.push(sig);
    }
  }

  std::vector<CompiledEdge> sorted;
  sorted.reserve(edges.size());
  std::set<size_t> processed_edges;

  while (!ready.empty()) {
    SignalId sig = ready.front();
    ready.pop();

    // Process all outgoing edges from this signal
    for (size_t idx : outgoing[sig]) {
      if (processed_edges.count(idx))
        continue;

      sorted.push_back(std::move(edges[idx]));
      processed_edges.insert(idx);

      // Decrease in-degree of target
      if (--in_degree[sorted.back().target] == 0) {
        ready.push(sorted.back().target);
      }
    }
  }

  if (sorted.size() != edges.size()) {
    throw std::runtime_error(
        "GraphCompiler: Topological sort failed (possible cycle)");
  }

  edges = std::move(sorted);
}

void GraphCompiler::detect_cycles(const std::vector<CompiledEdge> &edges) {
  // Build adjacency list
  std::map<SignalId, std::vector<SignalId>> graph;
  for (const auto &edge : edges) {
    graph[edge.source].push_back(edge.target);
  }

  // DFS-based cycle detection
  std::set<SignalId> visited;
  std::set<SignalId> rec_stack;

  std::function<bool(SignalId)> has_cycle = [&](SignalId node) -> bool {
    visited.insert(node);
    rec_stack.insert(node);

    for (SignalId neighbor : graph[node]) {
      if (!visited.count(neighbor)) {
        if (has_cycle(neighbor))
          return true;
      } else if (rec_stack.count(neighbor)) {
        return true; // Back edge found
      }
    }

    rec_stack.erase(node);
    return false;
  };

  for (const auto &[node, _] : graph) {
    if (!visited.count(node)) {
      if (has_cycle(node)) {
        throw std::runtime_error("GraphCompiler: Cycle detected (algebraic "
                                 "loop). Add DelayTransform to feedback path.");
      }
    }
  }
}

void GraphCompiler::validate_stability(
    const std::vector<std::unique_ptr<IModel>> &models, double expected_dt) {
  for (const auto &model : models) {
    double limit = model->compute_stability_limit();
    if (expected_dt > limit) {
      std::ostringstream oss;
      oss << "Stability violation: " << model->describe() << " requires dt < "
          << limit << "s, but dt = " << expected_dt << "s";
      throw std::runtime_error(oss.str());
    }
  }
}

} // namespace fluxgraph
