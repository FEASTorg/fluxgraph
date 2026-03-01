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
#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fluxgraph {

namespace {

const Variant &require_param(const std::map<std::string, Variant> &params,
                             const std::string &name,
                             const std::string &context) {
  auto it = params.find(name);
  if (it == params.end()) {
    throw std::runtime_error("Missing required parameter at " + context + "/" +
                             name);
  }
  return it->second;
}

std::string variant_type_name(const Variant &value) {
  if (std::holds_alternative<double>(value)) {
    return "double";
  }
  if (std::holds_alternative<int64_t>(value)) {
    return "int64";
  }
  if (std::holds_alternative<bool>(value)) {
    return "bool";
  }
  return "string";
}

double as_double(const Variant &value, const std::string &path) {
  if (std::holds_alternative<double>(value)) {
    return std::get<double>(value);
  }
  if (std::holds_alternative<int64_t>(value)) {
    return static_cast<double>(std::get<int64_t>(value));
  }
  throw std::runtime_error("Type error at " + path + ": expected number, got " +
                           variant_type_name(value));
}

int64_t as_int64(const Variant &value, const std::string &path) {
  if (std::holds_alternative<int64_t>(value)) {
    return std::get<int64_t>(value);
  }
  throw std::runtime_error("Type error at " + path + ": expected int64, got " +
                           variant_type_name(value));
}

std::string as_string(const Variant &value, const std::string &path) {
  if (std::holds_alternative<std::string>(value)) {
    return std::get<std::string>(value);
  }
  throw std::runtime_error("Type error at " + path + ": expected string, got " +
                           variant_type_name(value));
}

std::string trim_copy(const std::string &text) {
  auto begin = std::find_if_not(text.begin(), text.end(), [](unsigned char c) {
    return std::isspace(c) != 0;
  });
  auto end = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char c) {
               return std::isspace(c) != 0;
             }).base();

  if (begin >= end) {
    return "";
  }
  return std::string(begin, end);
}

std::function<bool(const SignalStore &)>
compile_condition_expr(const std::string &expr, SignalNamespace &signal_ns,
                       const std::string &rule_id) {
  static const std::regex kComparatorPattern(
      R"(^([A-Za-z0-9_./-]+)\s*(<=|>=|==|!=|<|>)\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)$)");

  const std::string trimmed = trim_copy(expr);
  std::smatch match;
  if (!std::regex_match(trimmed, match, kComparatorPattern)) {
    throw std::runtime_error("Unsupported rule condition syntax for rule '" +
                             rule_id +
                             "'. Supported form: <signal_path> <op> <number>");
  }

  const std::string signal_path = match[1].str();
  const std::string op = match[2].str();
  const double rhs = std::stod(match[3].str());
  const SignalId signal_id = signal_ns.intern(signal_path);

  if (op == "<") {
    return [signal_id, rhs](const SignalStore &store) {
      return store.read_value(signal_id) < rhs;
    };
  }
  if (op == "<=") {
    return [signal_id, rhs](const SignalStore &store) {
      return store.read_value(signal_id) <= rhs;
    };
  }
  if (op == ">") {
    return [signal_id, rhs](const SignalStore &store) {
      return store.read_value(signal_id) > rhs;
    };
  }
  if (op == ">=") {
    return [signal_id, rhs](const SignalStore &store) {
      return store.read_value(signal_id) >= rhs;
    };
  }
  if (op == "==") {
    return [signal_id, rhs](const SignalStore &store) {
      return store.read_value(signal_id) == rhs;
    };
  }

  // op == "!="
  return [signal_id, rhs](const SignalStore &store) {
    return store.read_value(signal_id) != rhs;
  };
}

} // namespace

GraphCompiler::GraphCompiler() = default;
GraphCompiler::~GraphCompiler() = default;

CompiledProgram GraphCompiler::compile(const GraphSpec &spec,
                                       SignalNamespace &signal_ns,
                                       FunctionNamespace &func_ns,
                                       double expected_dt) {
  CompiledProgram program;

  // Compile models
  for (const auto &model_spec : spec.models) {
    auto *model = parse_model(model_spec, signal_ns);
    program.models.emplace_back(model);
  }

  if (expected_dt > 0.0) {
    validate_stability(program.models, expected_dt);
  }

  // Compile edges
  for (const auto &edge_spec : spec.edges) {
    SignalId src = signal_ns.intern(edge_spec.source_path);
    SignalId tgt = signal_ns.intern(edge_spec.target_path);
    ITransform *tf = parse_transform(edge_spec.transform);
    const bool is_delay = edge_spec.transform.type == "delay";
    program.edges.emplace_back(src, tgt, tf, is_delay);
  }

  // Enforce single-writer ownership across model outputs and edge targets.
  std::map<SignalId, std::string> writer_owner;
  auto register_writer = [&writer_owner](SignalId id,
                                         const std::string &owner_desc) {
    auto [it, inserted] = writer_owner.emplace(id, owner_desc);
    if (!inserted) {
      throw std::runtime_error("Multiple writers for signal id " +
                               std::to_string(id) + ": '" + it->second +
                               "' conflicts with '" + owner_desc + "'");
    }
  };

  for (const auto &edge : program.edges) {
    register_writer(edge.target, "edge_target");
  }

  for (const auto &model_spec : spec.models) {
    if (model_spec.type == "thermal_mass") {
      const std::string temp_path = as_string(
          require_param(model_spec.params, "temp_signal",
                        "model[" + model_spec.id + ":" + model_spec.type + "]"),
          "model[" + model_spec.id + ":" + model_spec.type + "]/temp_signal");
      register_writer(signal_ns.intern(temp_path), "model_output");
    }
  }

  // Detect cycles in non-delay subgraph (delay edges explicitly break
  // algebraic loops)
  detect_cycles(program.edges);

  // Topological sort immediate-propagation edges; delay edges are evaluated
  // first in deterministic spec order.
  topological_sort(program.edges);

  // Compile rules
  for (const auto &rule_spec : spec.rules) {
    CompiledRule rule;
    rule.id = rule_spec.id;
    rule.on_error = rule_spec.on_error;

    rule.condition =
        compile_condition_expr(rule_spec.condition, signal_ns, rule_spec.id);

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
  const std::string context = "transform[" + type + "]";

  if (type == "linear") {
    double scale = as_double(require_param(spec.params, "scale", context),
                             context + "/scale");
    double offset = as_double(require_param(spec.params, "offset", context),
                              context + "/offset");
    double clamp_min = -std::numeric_limits<double>::infinity();
    double clamp_max = std::numeric_limits<double>::infinity();

    if (auto it = spec.params.find("clamp_min"); it != spec.params.end()) {
      clamp_min = as_double(it->second, context + "/clamp_min");
    }
    if (auto it = spec.params.find("clamp_max"); it != spec.params.end()) {
      clamp_max = as_double(it->second, context + "/clamp_max");
    }

    return new LinearTransform(scale, offset, clamp_min, clamp_max);
  } else if (type == "first_order_lag") {
    double tau_s = as_double(require_param(spec.params, "tau_s", context),
                             context + "/tau_s");
    return new FirstOrderLagTransform(tau_s);
  } else if (type == "delay") {
    double delay_sec =
        as_double(require_param(spec.params, "delay_sec", context),
                  context + "/delay_sec");
    return new DelayTransform(delay_sec);
  } else if (type == "noise") {
    double amplitude =
        as_double(require_param(spec.params, "amplitude", context),
                  context + "/amplitude");
    uint32_t seed = 0U;
    if (auto it = spec.params.find("seed"); it != spec.params.end()) {
      seed = static_cast<uint32_t>(as_int64(it->second, context + "/seed"));
    }
    return new NoiseTransform(amplitude, seed);
  } else if (type == "saturation") {
    double min_val = 0.0;
    double max_val = 0.0;
    if (auto it = spec.params.find("min"); it != spec.params.end()) {
      min_val = as_double(it->second, context + "/min");
    } else {
      min_val = as_double(require_param(spec.params, "min_value", context),
                          context + "/min_value");
    }

    if (auto it = spec.params.find("max"); it != spec.params.end()) {
      max_val = as_double(it->second, context + "/max");
    } else {
      max_val = as_double(require_param(spec.params, "max_value", context),
                          context + "/max_value");
    }
    return new SaturationTransform(min_val, max_val);
  } else if (type == "deadband") {
    double threshold =
        as_double(require_param(spec.params, "threshold", context),
                  context + "/threshold");
    return new DeadbandTransform(threshold);
  } else if (type == "rate_limiter") {
    double max_rate = 0.0;
    if (auto it = spec.params.find("max_rate_per_sec");
        it != spec.params.end()) {
      max_rate = as_double(it->second, context + "/max_rate_per_sec");
    } else {
      max_rate = as_double(require_param(spec.params, "max_rate", context),
                           context + "/max_rate");
    }
    return new RateLimiterTransform(max_rate);
  } else if (type == "moving_average") {
    int64_t window_size_raw =
        as_int64(require_param(spec.params, "window_size", context),
                 context + "/window_size");
    if (window_size_raw <= 0) {
      throw std::runtime_error("Invalid parameter at " + context +
                               "/window_size: expected >= 1");
    }
    size_t window_size = static_cast<size_t>(window_size_raw);
    return new MovingAverageTransform(window_size);
  } else {
    throw std::runtime_error("Unknown transform type: " + type);
  }
}

IModel *GraphCompiler::parse_model(const ModelSpec &spec, SignalNamespace &ns) {
  const std::string &type = spec.type;
  const std::string context = "model[" + spec.id + ":" + type + "]";

  if (type == "thermal_mass") {
    double thermal_mass =
        as_double(require_param(spec.params, "thermal_mass", context),
                  context + "/thermal_mass");
    double heat_transfer_coeff =
        as_double(require_param(spec.params, "heat_transfer_coeff", context),
                  context + "/heat_transfer_coeff");
    double initial_temp =
        as_double(require_param(spec.params, "initial_temp", context),
                  context + "/initial_temp");
    std::string temp_path =
        as_string(require_param(spec.params, "temp_signal", context),
                  context + "/temp_signal");
    std::string power_path =
        as_string(require_param(spec.params, "power_signal", context),
                  context + "/power_signal");
    std::string ambient_path =
        as_string(require_param(spec.params, "ambient_signal", context),
                  context + "/ambient_signal");

    return new ThermalMassModel(spec.id, thermal_mass, heat_transfer_coeff,
                                initial_temp, temp_path, power_path,
                                ambient_path, ns);
  } else {
    throw std::runtime_error("Unknown model type: " + type);
  }
}

void GraphCompiler::topological_sort(std::vector<CompiledEdge> &edges) {
  std::vector<size_t> delay_indices;
  std::vector<size_t> immediate_indices;
  delay_indices.reserve(edges.size());
  immediate_indices.reserve(edges.size());

  for (size_t i = 0; i < edges.size(); ++i) {
    if (edges[i].is_delay) {
      delay_indices.push_back(i);
    } else {
      immediate_indices.push_back(i);
    }
  }

  // Kahn's algorithm over immediate (non-delay) subgraph only.
  std::map<SignalId, std::vector<size_t>> outgoing;
  std::map<SignalId, int> in_degree;
  std::set<SignalId> all_signals;

  for (size_t idx : immediate_indices) {
    all_signals.insert(edges[idx].source);
    all_signals.insert(edges[idx].target);
    outgoing[edges[idx].source].push_back(idx);
    in_degree[edges[idx].target]++;
  }

  // Deterministic tie-break: smallest SignalId first.
  std::set<SignalId> ready;
  for (SignalId sig : all_signals) {
    if (in_degree[sig] == 0) {
      ready.insert(sig);
    }
  }

  std::vector<size_t> sorted_immediate_indices;
  sorted_immediate_indices.reserve(immediate_indices.size());
  std::set<size_t> processed_edges;

  while (!ready.empty()) {
    SignalId sig = *ready.begin();
    ready.erase(ready.begin());

    auto it = outgoing.find(sig);
    if (it == outgoing.end()) {
      continue;
    }

    for (size_t idx : it->second) {
      if (!processed_edges.insert(idx).second) {
        continue;
      }
      sorted_immediate_indices.push_back(idx);
      if (--in_degree[edges[idx].target] == 0) {
        ready.insert(edges[idx].target);
      }
    }
  }

  if (sorted_immediate_indices.size() != immediate_indices.size()) {
    throw std::runtime_error(
        "GraphCompiler: topological sort failed for non-delay edges.");
  }

  std::vector<CompiledEdge> sorted;
  sorted.reserve(edges.size());

  // Evaluate delay edges first so delayed signals are available for immediate
  // propagation stage.
  for (size_t idx : delay_indices) {
    sorted.push_back(std::move(edges[idx]));
  }

  for (size_t idx : sorted_immediate_indices) {
    sorted.push_back(std::move(edges[idx]));
  }

  edges = std::move(sorted);
}

void GraphCompiler::detect_cycles(const std::vector<CompiledEdge> &edges) {
  // Build adjacency list for non-delay edges only.
  std::map<SignalId, std::vector<SignalId>> graph;
  for (const auto &edge : edges) {
    if (edge.is_delay) {
      continue;
    }
    graph[edge.source].push_back(edge.target);
    if (graph.count(edge.target) == 0) {
      graph[edge.target] = {};
    }
  }

  std::map<SignalId, int> state; // 0=unvisited, 1=visiting, 2=done
  std::vector<SignalId> stack;
  std::vector<SignalId> cycle_path;
  bool found_cycle = false;

  std::function<void(SignalId)> dfs = [&](SignalId node) {
    if (found_cycle) {
      return;
    }

    state[node] = 1;
    stack.push_back(node);

    for (SignalId neighbor : graph[node]) {
      if (state[neighbor] == 0) {
        dfs(neighbor);
        if (found_cycle) {
          return;
        }
      } else if (state[neighbor] == 1) {
        auto start_it = std::find(stack.begin(), stack.end(), neighbor);
        cycle_path.assign(start_it, stack.end());
        cycle_path.push_back(neighbor);
        found_cycle = true;
        return;
      }
    }

    stack.pop_back();
    state[node] = 2;
  };

  for (const auto &[node, _] : graph) {
    if (state[node] == 0) {
      dfs(node);
    }
    if (found_cycle) {
      break;
    }
  }

  if (found_cycle) {
    std::ostringstream oss;
    oss << "GraphCompiler: Cycle detected in non-delay subgraph: ";
    for (size_t i = 0; i < cycle_path.size(); ++i) {
      if (i > 0) {
        oss << " -> ";
      }
      oss << cycle_path[i];
    }
    oss << ". Add a delay edge in feedback path.";
    throw std::runtime_error(oss.str());
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
