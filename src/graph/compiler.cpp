#include "fluxgraph/graph/compiler.hpp"
#include "fluxgraph/core/units.hpp"
#include "fluxgraph/model/dc_motor.hpp"
#include "fluxgraph/model/first_order_process.hpp"
#include "fluxgraph/model/integration.hpp"
#include "fluxgraph/model/mass_spring_damper.hpp"
#include "fluxgraph/model/second_order_process.hpp"
#include "fluxgraph/model/thermal_mass.hpp"
#include "fluxgraph/model/thermal_rc2.hpp"
#include "fluxgraph/transform/deadband.hpp"
#include "fluxgraph/transform/delay.hpp"
#include "fluxgraph/transform/first_order_lag.hpp"
#include "fluxgraph/transform/linear.hpp"
#include "fluxgraph/transform/moving_average.hpp"
#include "fluxgraph/transform/noise.hpp"
#include "fluxgraph/transform/rate_limiter.hpp"
#include "fluxgraph/transform/saturation.hpp"
#include "fluxgraph/transform/unit_convert.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

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

void require_finite(const double value, const std::string &path) {
  if (!std::isfinite(value)) {
    throw std::runtime_error("Invalid parameter at " + path +
                             ": expected finite number");
  }
}

void require_finite_positive(const double value, const std::string &path) {
  if (!std::isfinite(value) || value <= 0.0) {
    throw std::runtime_error("Invalid parameter at " + path + ": expected > 0");
  }
}

void require_finite_non_negative(const double value, const std::string &path) {
  if (!std::isfinite(value) || value < 0.0) {
    throw std::runtime_error("Invalid parameter at " + path +
                             ": expected >= 0");
  }
}

std::string format_scalar_constraint_rule(const ScalarConstraint &constraint) {
  switch (constraint.kind) {
  case ScalarConstraint::Kind::finite:
    return "finite number";
  case ScalarConstraint::Kind::greater_than:
    return "> " + std::to_string(constraint.a);
  case ScalarConstraint::Kind::greater_equal:
    return ">= " + std::to_string(constraint.a);
  case ScalarConstraint::Kind::closed_interval:
    return "in [" + std::to_string(constraint.a) + ", " +
           std::to_string(constraint.b) + "]";
  }
  return "finite number";
}

bool satisfies_scalar_constraint(const double value,
                                 const ScalarConstraint &constraint) {
  if (!std::isfinite(value)) {
    return false;
  }

  switch (constraint.kind) {
  case ScalarConstraint::Kind::finite:
    return true;
  case ScalarConstraint::Kind::greater_than:
    return value > constraint.a;
  case ScalarConstraint::Kind::greater_equal:
    return value >= constraint.a;
  case ScalarConstraint::Kind::closed_interval:
    return constraint.a <= constraint.b && value >= constraint.a &&
           value <= constraint.b;
  }
  return false;
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

const std::regex &rule_comparator_regex() {
  static const std::regex kComparatorPattern(
      R"(^([A-Za-z0-9_./-]+)\s*(<=|>=|==|!=|<|>)\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)$)");
  return kComparatorPattern;
}

std::function<bool(const SignalStore &)>
compile_condition_expr(const std::string &expr, SignalNamespace &signal_ns,
                       const std::string &rule_id) {
  const std::string trimmed = trim_copy(expr);
  std::smatch match;
  if (!std::regex_match(trimmed, match, rule_comparator_regex())) {
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

  return [signal_id, rhs](const SignalStore &store) {
    return store.read_value(signal_id) != rhs;
  };
}

struct TransformRegistryEntry {
  GraphCompiler::TransformFactory factory;
  bool has_signature = false;
  TransformSignature signature;
};

struct ModelRegistryEntry {
  GraphCompiler::ModelFactory factory;
  bool has_signature = false;
  ModelSignature signature;
};

struct FactoryRegistry {
  std::mutex mutex;
  bool defaults_registered = false;
  std::unordered_map<std::string, TransformRegistryEntry> transform_factories;
  std::unordered_map<std::string, ModelRegistryEntry> model_factories;
};

FactoryRegistry &factory_registry() {
  static FactoryRegistry registry;
  return registry;
}

void validate_registration_request(const std::string &type, bool has_factory,
                                   const std::string &kind) {
  if (trim_copy(type).empty()) {
    throw std::invalid_argument("GraphCompiler: " + kind +
                                " type must be non-empty");
  }
  if (!has_factory) {
    throw std::invalid_argument("GraphCompiler: " + kind +
                                " factory must be valid");
  }
}

void register_builtin_transform(FactoryRegistry &registry,
                                const std::string &type,
                                GraphCompiler::TransformFactory factory,
                                TransformSignature::Contract contract =
                                    TransformSignature::Contract::preserve) {
  TransformRegistryEntry entry;
  entry.factory = std::move(factory);
  entry.has_signature = true;
  entry.signature.contract = contract;
  registry.transform_factories.emplace(type, std::move(entry));
}

void register_builtin_model(FactoryRegistry &registry, const std::string &type,
                            GraphCompiler::ModelFactory factory,
                            ModelSignature signature) {
  ModelRegistryEntry entry;
  entry.factory = std::move(factory);
  entry.has_signature = true;
  entry.signature = std::move(signature);
  registry.model_factories.emplace(type, std::move(entry));
}

void ensure_default_factories_registered_locked(FactoryRegistry &registry) {
  if (registry.defaults_registered) {
    return;
  }

  register_builtin_transform(
      registry, "linear",
      [](const TransformSpec &spec) -> std::unique_ptr<ITransform> {
        const std::string context = "transform[linear]";
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

        return std::make_unique<LinearTransform>(scale, offset, clamp_min,
                                                 clamp_max);
      },
      TransformSignature::Contract::linear_conditioning);

  register_builtin_transform(
      registry, "first_order_lag",
      [](const TransformSpec &spec) -> std::unique_ptr<ITransform> {
        const std::string context = "transform[first_order_lag]";
        double tau_s = as_double(require_param(spec.params, "tau_s", context),
                                 context + "/tau_s");
        return std::make_unique<FirstOrderLagTransform>(tau_s);
      });

  register_builtin_transform(
      registry, "delay",
      [](const TransformSpec &spec) -> std::unique_ptr<ITransform> {
        const std::string context = "transform[delay]";
        double delay_sec =
            as_double(require_param(spec.params, "delay_sec", context),
                      context + "/delay_sec");
        return std::make_unique<DelayTransform>(delay_sec);
      });

  register_builtin_transform(
      registry, "noise",
      [](const TransformSpec &spec) -> std::unique_ptr<ITransform> {
        const std::string context = "transform[noise]";
        double amplitude =
            as_double(require_param(spec.params, "amplitude", context),
                      context + "/amplitude");
        uint32_t seed = 0U;
        if (auto it = spec.params.find("seed"); it != spec.params.end()) {
          seed = static_cast<uint32_t>(as_int64(it->second, context + "/seed"));
        }
        return std::make_unique<NoiseTransform>(amplitude, seed);
      });

  register_builtin_transform(
      registry, "saturation",
      [](const TransformSpec &spec) -> std::unique_ptr<ITransform> {
        const std::string context = "transform[saturation]";
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
        return std::make_unique<SaturationTransform>(min_val, max_val);
      });

  register_builtin_transform(
      registry, "deadband",
      [](const TransformSpec &spec) -> std::unique_ptr<ITransform> {
        const std::string context = "transform[deadband]";
        double threshold =
            as_double(require_param(spec.params, "threshold", context),
                      context + "/threshold");
        return std::make_unique<DeadbandTransform>(threshold);
      });

  register_builtin_transform(
      registry, "rate_limiter",
      [](const TransformSpec &spec) -> std::unique_ptr<ITransform> {
        const std::string context = "transform[rate_limiter]";
        double max_rate = 0.0;
        if (auto it = spec.params.find("max_rate_per_sec");
            it != spec.params.end()) {
          max_rate = as_double(it->second, context + "/max_rate_per_sec");
        } else {
          max_rate = as_double(require_param(spec.params, "max_rate", context),
                               context + "/max_rate");
        }
        return std::make_unique<RateLimiterTransform>(max_rate);
      });

  register_builtin_transform(
      registry, "moving_average",
      [](const TransformSpec &spec) -> std::unique_ptr<ITransform> {
        const std::string context = "transform[moving_average]";
        int64_t window_size_raw =
            as_int64(require_param(spec.params, "window_size", context),
                     context + "/window_size");
        if (window_size_raw <= 0) {
          throw std::runtime_error("Invalid parameter at " + context +
                                   "/window_size: expected >= 1");
        }
        size_t window_size = static_cast<size_t>(window_size_raw);
        return std::make_unique<MovingAverageTransform>(window_size);
      });

  register_builtin_transform(
      registry, "unit_convert",
      [](const TransformSpec &spec) -> std::unique_ptr<ITransform> {
        const std::string context = "transform[unit_convert]";
        double resolved_scale =
            as_double(require_param(spec.params, "__resolved_scale", context),
                      context + "/__resolved_scale");
        double resolved_offset =
            as_double(require_param(spec.params, "__resolved_offset", context),
                      context + "/__resolved_offset");
        return std::make_unique<UnitConvertTransform>(resolved_scale,
                                                      resolved_offset);
      },
      TransformSignature::Contract::unit_convert);

  ModelSignature thermal_signature;
  thermal_signature.signal_param_units.emplace("power_signal", "W");
  thermal_signature.signal_param_units.emplace("ambient_signal", "degC");
  thermal_signature.signal_param_units.emplace("temp_signal", "degC");
  thermal_signature.scalar_param_signatures.emplace(
      "thermal_mass",
      ScalarParamSignature{"J/K", ScalarConstraint::greater_than(0.0), true});
  thermal_signature.scalar_param_signatures.emplace(
      "heat_transfer_coeff",
      ScalarParamSignature{"W/K", ScalarConstraint::greater_than(0.0), true});
  thermal_signature.scalar_param_signatures.emplace(
      "initial_temp",
      ScalarParamSignature{"degC", ScalarConstraint::finite_only(), true});

  register_builtin_model(
      registry, "thermal_mass",
      [](const ModelSpec &spec,
         SignalNamespace &ns) -> std::unique_ptr<IModel> {
        const std::string context = "model[" + spec.id + ":thermal_mass]";
        const std::string thermal_mass_path = context + "/thermal_mass";
        const std::string heat_transfer_coeff_path =
            context + "/heat_transfer_coeff";
        const std::string initial_temp_path = context + "/initial_temp";
        double thermal_mass =
            as_double(require_param(spec.params, "thermal_mass", context),
                      thermal_mass_path);
        double heat_transfer_coeff = as_double(
            require_param(spec.params, "heat_transfer_coeff", context),
            heat_transfer_coeff_path);
        double initial_temp =
            as_double(require_param(spec.params, "initial_temp", context),
                      initial_temp_path);

        require_finite_positive(thermal_mass, thermal_mass_path);
        require_finite_positive(heat_transfer_coeff, heat_transfer_coeff_path);
        require_finite(initial_temp, initial_temp_path);

        std::string temp_path =
            as_string(require_param(spec.params, "temp_signal", context),
                      context + "/temp_signal");
        std::string power_path =
            as_string(require_param(spec.params, "power_signal", context),
                      context + "/power_signal");
        std::string ambient_path =
            as_string(require_param(spec.params, "ambient_signal", context),
                      context + "/ambient_signal");
        ThermalIntegrationMethod integration_method =
            ThermalIntegrationMethod::ForwardEuler;
        if (auto it = spec.params.find("integration_method");
            it != spec.params.end()) {
          const std::string method_name =
              as_string(it->second, context + "/integration_method");
          try {
            integration_method = parse_thermal_integration_method(method_name);
          } catch (const std::invalid_argument &e) {
            throw std::runtime_error("Invalid parameter at " + context +
                                     "/integration_method: " + e.what());
          }
        }

        return std::make_unique<ThermalMassModel>(
            spec.id, thermal_mass, heat_transfer_coeff, initial_temp, temp_path,
            power_path, ambient_path, ns, integration_method);
      },
      thermal_signature);

  ModelSignature thermal_rc2_signature;
  thermal_rc2_signature.signal_param_units.emplace("power_signal", "W");
  thermal_rc2_signature.signal_param_units.emplace("ambient_signal", "degC");
  thermal_rc2_signature.signal_param_units.emplace("temp_signal_a", "degC");
  thermal_rc2_signature.signal_param_units.emplace("temp_signal_b", "degC");

  thermal_rc2_signature.scalar_param_signatures.emplace(
      "thermal_mass_a",
      ScalarParamSignature{"J/K", ScalarConstraint::greater_than(0.0), true});
  thermal_rc2_signature.scalar_param_signatures.emplace(
      "thermal_mass_b",
      ScalarParamSignature{"J/K", ScalarConstraint::greater_than(0.0), true});
  thermal_rc2_signature.scalar_param_signatures.emplace(
      "heat_transfer_coeff_a",
      ScalarParamSignature{"W/K", ScalarConstraint::greater_than(0.0), true});
  thermal_rc2_signature.scalar_param_signatures.emplace(
      "heat_transfer_coeff_b",
      ScalarParamSignature{"W/K", ScalarConstraint::greater_than(0.0), true});
  thermal_rc2_signature.scalar_param_signatures.emplace(
      "coupling_coeff",
      ScalarParamSignature{"W/K", ScalarConstraint::greater_equal(0.0), true});
  thermal_rc2_signature.scalar_param_signatures.emplace(
      "initial_temp_a",
      ScalarParamSignature{"degC", ScalarConstraint::finite_only(), true});
  thermal_rc2_signature.scalar_param_signatures.emplace(
      "initial_temp_b",
      ScalarParamSignature{"degC", ScalarConstraint::finite_only(), true});

  register_builtin_model(
      registry, "thermal_rc2",
      [](const ModelSpec &spec,
         SignalNamespace &ns) -> std::unique_ptr<IModel> {
        const std::string context = "model[" + spec.id + ":thermal_rc2]";

        const std::string thermal_mass_a_path = context + "/thermal_mass_a";
        const std::string thermal_mass_b_path = context + "/thermal_mass_b";
        const std::string heat_transfer_coeff_a_path =
            context + "/heat_transfer_coeff_a";
        const std::string heat_transfer_coeff_b_path =
            context + "/heat_transfer_coeff_b";
        const std::string coupling_coeff_path = context + "/coupling_coeff";
        const std::string initial_temp_a_path = context + "/initial_temp_a";
        const std::string initial_temp_b_path = context + "/initial_temp_b";

        double thermal_mass_a =
            as_double(require_param(spec.params, "thermal_mass_a", context),
                      thermal_mass_a_path);
        double thermal_mass_b =
            as_double(require_param(spec.params, "thermal_mass_b", context),
                      thermal_mass_b_path);
        double heat_transfer_coeff_a = as_double(
            require_param(spec.params, "heat_transfer_coeff_a", context),
            heat_transfer_coeff_a_path);
        double heat_transfer_coeff_b = as_double(
            require_param(spec.params, "heat_transfer_coeff_b", context),
            heat_transfer_coeff_b_path);
        double coupling_coeff =
            as_double(require_param(spec.params, "coupling_coeff", context),
                      coupling_coeff_path);
        double initial_temp_a =
            as_double(require_param(spec.params, "initial_temp_a", context),
                      initial_temp_a_path);
        double initial_temp_b =
            as_double(require_param(spec.params, "initial_temp_b", context),
                      initial_temp_b_path);

        require_finite_positive(thermal_mass_a, thermal_mass_a_path);
        require_finite_positive(thermal_mass_b, thermal_mass_b_path);
        require_finite_positive(heat_transfer_coeff_a,
                                heat_transfer_coeff_a_path);
        require_finite_positive(heat_transfer_coeff_b,
                                heat_transfer_coeff_b_path);
        require_finite_non_negative(coupling_coeff, coupling_coeff_path);
        require_finite(initial_temp_a, initial_temp_a_path);
        require_finite(initial_temp_b, initial_temp_b_path);

        std::string temp_a_path =
            as_string(require_param(spec.params, "temp_signal_a", context),
                      context + "/temp_signal_a");
        std::string temp_b_path =
            as_string(require_param(spec.params, "temp_signal_b", context),
                      context + "/temp_signal_b");
        std::string power_path =
            as_string(require_param(spec.params, "power_signal", context),
                      context + "/power_signal");
        std::string ambient_path =
            as_string(require_param(spec.params, "ambient_signal", context),
                      context + "/ambient_signal");

        ThermalIntegrationMethod integration_method =
            ThermalIntegrationMethod::ForwardEuler;
        if (auto it = spec.params.find("integration_method");
            it != spec.params.end()) {
          const std::string method_name =
              as_string(it->second, context + "/integration_method");
          try {
            integration_method = parse_thermal_integration_method(method_name);
          } catch (const std::invalid_argument &e) {
            throw std::runtime_error("Invalid parameter at " + context +
                                     "/integration_method: " + e.what());
          }
        }

        return std::make_unique<ThermalRc2Model>(
            spec.id, thermal_mass_a, thermal_mass_b, heat_transfer_coeff_a,
            heat_transfer_coeff_b, coupling_coeff, initial_temp_a,
            initial_temp_b, temp_a_path, temp_b_path, power_path, ambient_path,
            ns, integration_method);
      },
      thermal_rc2_signature);

  ModelSignature first_order_signature;
  first_order_signature.signal_param_units.emplace("input_signal",
                                                   "dimensionless");
  first_order_signature.signal_param_units.emplace("output_signal",
                                                   "dimensionless");
  first_order_signature.scalar_param_signatures.emplace(
      "gain", ScalarParamSignature{"dimensionless",
                                   ScalarConstraint::finite_only(), true});
  first_order_signature.scalar_param_signatures.emplace(
      "tau_s",
      ScalarParamSignature{"s", ScalarConstraint::greater_than(0.0), true});
  first_order_signature.scalar_param_signatures.emplace(
      "initial_output",
      ScalarParamSignature{"dimensionless", ScalarConstraint::finite_only(),
                           true});

  register_builtin_model(
      registry, "first_order_process",
      [](const ModelSpec &spec,
         SignalNamespace &ns) -> std::unique_ptr<IModel> {
        const std::string context =
            "model[" + spec.id + ":first_order_process]";

        const std::string gain_path = context + "/gain";
        const std::string tau_path = context + "/tau_s";
        const std::string initial_output_path = context + "/initial_output";

        const double gain =
            as_double(require_param(spec.params, "gain", context), gain_path);
        const double tau_s =
            as_double(require_param(spec.params, "tau_s", context), tau_path);
        const double initial_output =
            as_double(require_param(spec.params, "initial_output", context),
                      initial_output_path);

        require_finite(gain, gain_path);
        require_finite_positive(tau_s, tau_path);
        require_finite(initial_output, initial_output_path);

        const std::string output_path =
            as_string(require_param(spec.params, "output_signal", context),
                      context + "/output_signal");
        const std::string input_path =
            as_string(require_param(spec.params, "input_signal", context),
                      context + "/input_signal");

        IntegrationMethod integration_method = IntegrationMethod::ForwardEuler;
        if (auto it = spec.params.find("integration_method");
            it != spec.params.end()) {
          const std::string method_name =
              as_string(it->second, context + "/integration_method");
          try {
            integration_method = parse_integration_method(method_name);
          } catch (const std::invalid_argument &e) {
            throw std::runtime_error("Invalid parameter at " + context +
                                     "/integration_method: " + e.what());
          }
        }

        return std::make_unique<FirstOrderProcessModel>(
            spec.id, gain, tau_s, initial_output, output_path, input_path, ns,
            integration_method);
      },
      first_order_signature);

  ModelSignature second_order_signature;
  second_order_signature.signal_param_units.emplace("input_signal",
                                                    "dimensionless");
  second_order_signature.signal_param_units.emplace("output_signal",
                                                    "dimensionless");
  second_order_signature.scalar_param_signatures.emplace(
      "gain", ScalarParamSignature{"dimensionless",
                                   ScalarConstraint::finite_only(), true});
  second_order_signature.scalar_param_signatures.emplace(
      "zeta", ScalarParamSignature{"dimensionless",
                                   ScalarConstraint::greater_equal(0.0), true});
  second_order_signature.scalar_param_signatures.emplace(
      "omega_n_rad_s",
      ScalarParamSignature{"1/s", ScalarConstraint::greater_than(0.0), true});
  second_order_signature.scalar_param_signatures.emplace(
      "initial_output",
      ScalarParamSignature{"dimensionless", ScalarConstraint::finite_only(),
                           true});
  second_order_signature.scalar_param_signatures.emplace(
      "initial_output_rate",
      ScalarParamSignature{"1/s", ScalarConstraint::finite_only(), true});

  register_builtin_model(
      registry, "second_order_process",
      [](const ModelSpec &spec,
         SignalNamespace &ns) -> std::unique_ptr<IModel> {
        const std::string context =
            "model[" + spec.id + ":second_order_process]";

        const std::string gain_path = context + "/gain";
        const std::string zeta_path = context + "/zeta";
        const std::string omega_path = context + "/omega_n_rad_s";
        const std::string initial_output_path = context + "/initial_output";
        const std::string initial_output_rate_path =
            context + "/initial_output_rate";

        const double gain =
            as_double(require_param(spec.params, "gain", context), gain_path);
        const double zeta =
            as_double(require_param(spec.params, "zeta", context), zeta_path);
        const double omega_n_rad_s = as_double(
            require_param(spec.params, "omega_n_rad_s", context), omega_path);
        const double initial_output =
            as_double(require_param(spec.params, "initial_output", context),
                      initial_output_path);
        const double initial_output_rate = as_double(
            require_param(spec.params, "initial_output_rate", context),
            initial_output_rate_path);

        require_finite(gain, gain_path);
        require_finite_non_negative(zeta, zeta_path);
        require_finite_positive(omega_n_rad_s, omega_path);
        require_finite(initial_output, initial_output_path);
        require_finite(initial_output_rate, initial_output_rate_path);

        const std::string output_path =
            as_string(require_param(spec.params, "output_signal", context),
                      context + "/output_signal");
        const std::string input_path =
            as_string(require_param(spec.params, "input_signal", context),
                      context + "/input_signal");

        IntegrationMethod integration_method = IntegrationMethod::ForwardEuler;
        if (auto it = spec.params.find("integration_method");
            it != spec.params.end()) {
          const std::string method_name =
              as_string(it->second, context + "/integration_method");
          try {
            integration_method = parse_integration_method(method_name);
          } catch (const std::invalid_argument &e) {
            throw std::runtime_error("Invalid parameter at " + context +
                                     "/integration_method: " + e.what());
          }
        }

        return std::make_unique<SecondOrderProcessModel>(
            spec.id, gain, zeta, omega_n_rad_s, initial_output,
            initial_output_rate, output_path, input_path, ns,
            integration_method);
      },
      second_order_signature);

  ModelSignature mass_spring_signature;
  mass_spring_signature.signal_param_units.emplace("force_signal", "N");
  mass_spring_signature.signal_param_units.emplace("position_signal", "m");
  mass_spring_signature.signal_param_units.emplace("velocity_signal", "m/s");
  mass_spring_signature.scalar_param_signatures.emplace(
      "mass",
      ScalarParamSignature{"kg", ScalarConstraint::greater_than(0.0), true});
  mass_spring_signature.scalar_param_signatures.emplace(
      "damping_coeff",
      ScalarParamSignature{"N*s/m", ScalarConstraint::greater_equal(0.0),
                           true});
  mass_spring_signature.scalar_param_signatures.emplace(
      "spring_constant",
      ScalarParamSignature{"N/m", ScalarConstraint::greater_equal(0.0), true});
  mass_spring_signature.scalar_param_signatures.emplace(
      "initial_position",
      ScalarParamSignature{"m", ScalarConstraint::finite_only(), true});
  mass_spring_signature.scalar_param_signatures.emplace(
      "initial_velocity",
      ScalarParamSignature{"m/s", ScalarConstraint::finite_only(), true});

  register_builtin_model(
      registry, "mass_spring_damper",
      [](const ModelSpec &spec,
         SignalNamespace &ns) -> std::unique_ptr<IModel> {
        const std::string context = "model[" + spec.id + ":mass_spring_damper]";

        const std::string mass_path = context + "/mass";
        const std::string damping_path = context + "/damping_coeff";
        const std::string spring_path = context + "/spring_constant";
        const std::string initial_position_path = context + "/initial_position";
        const std::string initial_velocity_path = context + "/initial_velocity";

        const double mass =
            as_double(require_param(spec.params, "mass", context), mass_path);
        const double damping_coeff = as_double(
            require_param(spec.params, "damping_coeff", context), damping_path);
        const double spring_constant =
            as_double(require_param(spec.params, "spring_constant", context),
                      spring_path);
        const double initial_position =
            as_double(require_param(spec.params, "initial_position", context),
                      initial_position_path);
        const double initial_velocity =
            as_double(require_param(spec.params, "initial_velocity", context),
                      initial_velocity_path);

        require_finite_positive(mass, mass_path);
        require_finite_non_negative(damping_coeff, damping_path);
        require_finite_non_negative(spring_constant, spring_path);
        require_finite(initial_position, initial_position_path);
        require_finite(initial_velocity, initial_velocity_path);

        const std::string position_path =
            as_string(require_param(spec.params, "position_signal", context),
                      context + "/position_signal");
        const std::string velocity_path =
            as_string(require_param(spec.params, "velocity_signal", context),
                      context + "/velocity_signal");
        const std::string force_path =
            as_string(require_param(spec.params, "force_signal", context),
                      context + "/force_signal");

        IntegrationMethod integration_method = IntegrationMethod::ForwardEuler;
        if (auto it = spec.params.find("integration_method");
            it != spec.params.end()) {
          const std::string method_name =
              as_string(it->second, context + "/integration_method");
          try {
            integration_method = parse_integration_method(method_name);
          } catch (const std::invalid_argument &e) {
            throw std::runtime_error("Invalid parameter at " + context +
                                     "/integration_method: " + e.what());
          }
        }

        return std::make_unique<MassSpringDamperModel>(
            spec.id, mass, damping_coeff, spring_constant, initial_position,
            initial_velocity, position_path, velocity_path, force_path, ns,
            integration_method);
      },
      mass_spring_signature);

  ModelSignature dc_motor_signature;
  dc_motor_signature.signal_param_units.emplace("voltage_signal", "V");
  dc_motor_signature.signal_param_units.emplace("load_torque_signal", "N*m");
  dc_motor_signature.signal_param_units.emplace("speed_signal", "rad/s");
  dc_motor_signature.signal_param_units.emplace("current_signal", "A");
  dc_motor_signature.signal_param_units.emplace("torque_signal", "N*m");

  dc_motor_signature.scalar_param_signatures.emplace(
      "resistance_ohm",
      ScalarParamSignature{"Ohm", ScalarConstraint::greater_than(0.0), true});
  dc_motor_signature.scalar_param_signatures.emplace(
      "inductance_h",
      ScalarParamSignature{"H", ScalarConstraint::greater_than(0.0), true});
  dc_motor_signature.scalar_param_signatures.emplace(
      "torque_constant",
      ScalarParamSignature{"N*m/A", ScalarConstraint::greater_than(0.0), true});
  dc_motor_signature.scalar_param_signatures.emplace(
      "back_emf_constant",
      ScalarParamSignature{"V*s/rad", ScalarConstraint::greater_than(0.0),
                           true});
  dc_motor_signature.scalar_param_signatures.emplace(
      "inertia", ScalarParamSignature{
                     "kg*m^2", ScalarConstraint::greater_than(0.0), true});
  dc_motor_signature.scalar_param_signatures.emplace(
      "viscous_friction",
      ScalarParamSignature{"N*m*s/rad", ScalarConstraint::greater_equal(0.0),
                           true});
  dc_motor_signature.scalar_param_signatures.emplace(
      "initial_current",
      ScalarParamSignature{"A", ScalarConstraint::finite_only(), true});
  dc_motor_signature.scalar_param_signatures.emplace(
      "initial_speed",
      ScalarParamSignature{"rad/s", ScalarConstraint::finite_only(), true});

  register_builtin_model(
      registry, "dc_motor",
      [](const ModelSpec &spec,
         SignalNamespace &ns) -> std::unique_ptr<IModel> {
        const std::string context = "model[" + spec.id + ":dc_motor]";

        const std::string resistance_path = context + "/resistance_ohm";
        const std::string inductance_path = context + "/inductance_h";
        const std::string torque_constant_path = context + "/torque_constant";
        const std::string back_emf_constant_path =
            context + "/back_emf_constant";
        const std::string inertia_path = context + "/inertia";
        const std::string viscous_friction_path = context + "/viscous_friction";
        const std::string initial_current_path = context + "/initial_current";
        const std::string initial_speed_path = context + "/initial_speed";

        const double resistance_ohm =
            as_double(require_param(spec.params, "resistance_ohm", context),
                      resistance_path);
        const double inductance_h =
            as_double(require_param(spec.params, "inductance_h", context),
                      inductance_path);
        const double torque_constant =
            as_double(require_param(spec.params, "torque_constant", context),
                      torque_constant_path);
        const double back_emf_constant =
            as_double(require_param(spec.params, "back_emf_constant", context),
                      back_emf_constant_path);
        const double inertia = as_double(
            require_param(spec.params, "inertia", context), inertia_path);
        const double viscous_friction =
            as_double(require_param(spec.params, "viscous_friction", context),
                      viscous_friction_path);
        const double initial_current =
            as_double(require_param(spec.params, "initial_current", context),
                      initial_current_path);
        const double initial_speed =
            as_double(require_param(spec.params, "initial_speed", context),
                      initial_speed_path);

        require_finite_positive(resistance_ohm, resistance_path);
        require_finite_positive(inductance_h, inductance_path);
        require_finite_positive(torque_constant, torque_constant_path);
        require_finite_positive(back_emf_constant, back_emf_constant_path);
        require_finite_positive(inertia, inertia_path);
        require_finite_non_negative(viscous_friction, viscous_friction_path);
        require_finite(initial_current, initial_current_path);
        require_finite(initial_speed, initial_speed_path);

        const std::string speed_path =
            as_string(require_param(spec.params, "speed_signal", context),
                      context + "/speed_signal");
        const std::string current_path =
            as_string(require_param(spec.params, "current_signal", context),
                      context + "/current_signal");
        const std::string torque_path =
            as_string(require_param(spec.params, "torque_signal", context),
                      context + "/torque_signal");
        const std::string voltage_path =
            as_string(require_param(spec.params, "voltage_signal", context),
                      context + "/voltage_signal");
        const std::string load_torque_path =
            as_string(require_param(spec.params, "load_torque_signal", context),
                      context + "/load_torque_signal");

        IntegrationMethod integration_method = IntegrationMethod::ForwardEuler;
        if (auto it = spec.params.find("integration_method");
            it != spec.params.end()) {
          const std::string method_name =
              as_string(it->second, context + "/integration_method");
          try {
            integration_method = parse_integration_method(method_name);
          } catch (const std::invalid_argument &e) {
            throw std::runtime_error("Invalid parameter at " + context +
                                     "/integration_method: " + e.what());
          }
        }

        return std::make_unique<DcMotorModel>(
            spec.id, resistance_ohm, inductance_h, torque_constant,
            back_emf_constant, inertia, viscous_friction, initial_current,
            initial_speed, speed_path, current_path, torque_path, voltage_path,
            load_torque_path, ns, integration_method);
      },
      dc_motor_signature);

  registry.defaults_registered = true;
}

const TransformRegistryEntry &
resolve_transform_entry_or_throw(FactoryRegistry &registry,
                                 const std::string &type) {
  auto it = registry.transform_factories.find(type);
  if (it == registry.transform_factories.end()) {
    throw std::runtime_error("Unknown transform type: " + type);
  }
  return it->second;
}

const ModelRegistryEntry &
resolve_model_entry_or_throw(FactoryRegistry &registry,
                             const std::string &type) {
  auto it = registry.model_factories.find(type);
  if (it == registry.model_factories.end()) {
    throw std::runtime_error("Unknown model type: " + type);
  }
  return it->second;
}

void emit_warning(const CompilationOptions &options,
                  const std::string &message) {
  if (options.warning_handler) {
    options.warning_handler(message);
  }
}

bool is_unit_known(const UnitRegistry &registry, const std::string &unit) {
  return registry.contains(unit);
}

bool has_compatible_dimension_and_kind(const UnitRegistry &registry,
                                       const std::string &lhs_unit,
                                       const std::string &rhs_unit) {
  const UnitDef *lhs = registry.find(lhs_unit);
  const UnitDef *rhs = registry.find(rhs_unit);
  if (lhs == nullptr || rhs == nullptr) {
    return false;
  }
  return lhs->dimension == rhs->dimension && lhs->kind == rhs->kind;
}

std::string resolve_signal_contract_or_empty(
    const std::unordered_map<SignalId, std::string> &signal_contracts,
    SignalId id) {
  auto it = signal_contracts.find(id);
  if (it == signal_contracts.end()) {
    return "";
  }
  return it->second;
}

void validate_model_signature_contracts(
    const ModelSpec &model_spec, const ModelSignature &signature,
    SignalNamespace &signal_ns,
    const std::unordered_map<SignalId, std::string> &signal_contracts,
    const UnitRegistry &unit_registry, const CompilationOptions &options,
    bool strict) {
  for (const auto &[param_name, expected_unit] : signature.signal_param_units) {
    auto param_it = model_spec.params.find(param_name);
    if (param_it == model_spec.params.end()) {
      continue;
    }

    const std::string path = as_string(
        param_it->second, "model[" + model_spec.id + ":" + model_spec.type +
                              "]/{" + param_name + "}");
    const SignalId id = signal_ns.intern(path);
    const std::string actual_unit =
        resolve_signal_contract_or_empty(signal_contracts, id);

    if (actual_unit.empty()) {
      if (strict) {
        throw std::runtime_error(
            "GraphCompiler: strict mode requires declared signal contract "
            "for model '" +
            model_spec.id + "' parameter '" + param_name + "' (path '" + path +
            "')");
      }
      continue;
    }

    if (actual_unit != expected_unit) {
      const std::string message = "GraphCompiler: model '" + model_spec.id +
                                  "' parameter '" + param_name +
                                  "' expects unit '" + expected_unit +
                                  "' but found '" + actual_unit + "'";
      if (strict) {
        throw std::runtime_error(message);
      }
      emit_warning(options, message);
    }
  }

  for (const auto &[param_name, param_signature] :
       signature.scalar_param_signatures) {
    auto param_it = model_spec.params.find(param_name);
    const std::string path =
        "model[" + model_spec.id + ":" + model_spec.type + "]/" + param_name;

    if (param_it == model_spec.params.end()) {
      if (strict && param_signature.required) {
        throw std::runtime_error("GraphCompiler: strict mode requires scalar "
                                 "parameter '" +
                                 param_name + "' for model '" + model_spec.id +
                                 "'");
      }
      if (!strict && param_signature.required) {
        emit_warning(options,
                     "GraphCompiler: missing required scalar parameter '" +
                         param_name + "' for model '" + model_spec.id + "'");
      }
      continue;
    }

    if (!param_signature.unit_symbol.empty() &&
        !is_unit_known(unit_registry, param_signature.unit_symbol)) {
      const std::string message =
          "GraphCompiler: model signature for type '" + model_spec.type +
          "' references unknown scalar unit symbol '" +
          param_signature.unit_symbol + "' for parameter '" + param_name + "'";
      if (strict) {
        throw std::runtime_error(message);
      }
      emit_warning(options, message);
    }

    const double value = as_double(param_it->second, path);
    if (!satisfies_scalar_constraint(value, param_signature.constraint)) {
      const std::string message =
          "Invalid parameter at " + path + ": expected " +
          format_scalar_constraint_rule(param_signature.constraint);
      if (strict) {
        throw std::runtime_error(message);
      }
      emit_warning(options, message);
    }
  }
}

} // namespace

GraphCompiler::GraphCompiler() = default;
GraphCompiler::~GraphCompiler() = default;

void GraphCompiler::register_transform_factory(const std::string &type,
                                               TransformFactory factory) {
  validate_registration_request(type, static_cast<bool>(factory), "transform");

  auto &registry = factory_registry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  ensure_default_factories_registered_locked(registry);

  TransformRegistryEntry entry;
  entry.factory = std::move(factory);

  auto [_, inserted] =
      registry.transform_factories.emplace(type, std::move(entry));
  if (!inserted) {
    throw std::runtime_error("GraphCompiler: transform factory already "
                             "registered for type '" +
                             type + "'");
  }
}

void GraphCompiler::register_transform_factory_with_signature(
    const std::string &type, TransformFactory factory,
    const TransformSignature &signature) {
  validate_registration_request(type, static_cast<bool>(factory), "transform");

  auto &registry = factory_registry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  ensure_default_factories_registered_locked(registry);

  TransformRegistryEntry entry;
  entry.factory = std::move(factory);
  entry.has_signature = true;
  entry.signature = signature;

  auto [_, inserted] =
      registry.transform_factories.emplace(type, std::move(entry));
  if (!inserted) {
    throw std::runtime_error("GraphCompiler: transform factory already "
                             "registered for type '" +
                             type + "'");
  }
}

void GraphCompiler::register_model_factory(const std::string &type,
                                           ModelFactory factory) {
  validate_registration_request(type, static_cast<bool>(factory), "model");

  auto &registry = factory_registry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  ensure_default_factories_registered_locked(registry);

  ModelRegistryEntry entry;
  entry.factory = std::move(factory);

  auto [_, inserted] = registry.model_factories.emplace(type, std::move(entry));
  if (!inserted) {
    throw std::runtime_error("GraphCompiler: model factory already registered "
                             "for type '" +
                             type + "'");
  }
}

void GraphCompiler::register_model_factory_with_signature(
    const std::string &type, ModelFactory factory,
    const ModelSignature &signature) {
  validate_registration_request(type, static_cast<bool>(factory), "model");

  auto &registry = factory_registry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  ensure_default_factories_registered_locked(registry);

  ModelRegistryEntry entry;
  entry.factory = std::move(factory);
  entry.has_signature = true;
  entry.signature = signature;

  auto [_, inserted] = registry.model_factories.emplace(type, std::move(entry));
  if (!inserted) {
    throw std::runtime_error("GraphCompiler: model factory already registered "
                             "for type '" +
                             type + "'");
  }
}

bool GraphCompiler::is_transform_registered(const std::string &type) {
  auto &registry = factory_registry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  ensure_default_factories_registered_locked(registry);
  return registry.transform_factories.find(type) !=
         registry.transform_factories.end();
}

bool GraphCompiler::is_model_registered(const std::string &type) {
  auto &registry = factory_registry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  ensure_default_factories_registered_locked(registry);
  return registry.model_factories.find(type) != registry.model_factories.end();
}

CompiledProgram GraphCompiler::compile(const GraphSpec &spec,
                                       SignalNamespace &signal_ns,
                                       FunctionNamespace &func_ns,
                                       double expected_dt) {
  CompilationOptions options;
  options.expected_dt = expected_dt;
  return compile(spec, signal_ns, func_ns, options);
}

CompiledProgram GraphCompiler::compile(const GraphSpec &spec,
                                       SignalNamespace &signal_ns,
                                       FunctionNamespace &func_ns,
                                       const CompilationOptions &options) {
  CompiledProgram program;
  const UnitRegistry &unit_registry = UnitRegistry::instance();

  const bool strict = options.dimensional_policy == DimensionalPolicy::strict;

  std::unordered_map<SignalId, std::string> signal_contracts;
  signal_contracts.reserve(spec.signals.size());

  for (size_t i = 0; i < spec.signals.size(); ++i) {
    const auto &signal_spec = spec.signals[i];
    if (trim_copy(signal_spec.path).empty()) {
      throw std::runtime_error("GraphCompiler: signals[" + std::to_string(i) +
                               "].path must be non-empty");
    }

    const std::string unit = trim_copy(signal_spec.unit);
    if (unit.empty()) {
      throw std::runtime_error("GraphCompiler: signals[" + std::to_string(i) +
                               "].unit must be non-empty");
    }

    const SignalId id = signal_ns.intern(signal_spec.path);
    const auto existing = signal_contracts.find(id);
    if (existing != signal_contracts.end() && existing->second != unit) {
      throw std::runtime_error(
          "GraphCompiler: duplicate signal contract for '" + signal_spec.path +
          "' with conflicting units ('" + existing->second + "' vs '" + unit +
          "')");
    }

    if (!is_unit_known(unit_registry, unit)) {
      if (strict) {
        throw std::runtime_error(
            "GraphCompiler: unknown unit symbol in signals "
            "contract for path '" +
            signal_spec.path + "': '" + unit + "'");
      }
      emit_warning(options,
                   "GraphCompiler: unknown unit symbol in permissive mode for "
                   "signal path '" +
                       signal_spec.path + "': '" + unit + "'");
    }

    signal_contracts[id] = unit;
  }

  {
    auto &registry = factory_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    ensure_default_factories_registered_locked(registry);

    for (size_t i = 0; i < spec.models.size(); ++i) {
      const auto &model_spec = spec.models[i];
      const auto &entry =
          resolve_model_entry_or_throw(registry, model_spec.type);
      if (strict && !entry.has_signature) {
        throw std::runtime_error(
            "GraphCompiler: strict mode requires signature metadata for model "
            "type '" +
            model_spec.type + "' (model id '" + model_spec.id + "')");
      }

      if (entry.has_signature) {
        validate_model_signature_contracts(model_spec, entry.signature,
                                           signal_ns, signal_contracts,
                                           unit_registry, options, strict);
      }
    }
  }

  // Compile models.
  for (const auto &model_spec : spec.models) {
    auto *model = parse_model(model_spec, signal_ns);
    program.models.emplace_back(model);
  }

  if (options.expected_dt > 0.0) {
    validate_stability(program.models, options.expected_dt);
  }

  // Compile edges with dimensional checks.
  for (size_t edge_index = 0; edge_index < spec.edges.size(); ++edge_index) {
    const auto &edge_spec = spec.edges[edge_index];

    const SignalId src = signal_ns.intern(edge_spec.source_path);
    const SignalId tgt = signal_ns.intern(edge_spec.target_path);

    const std::string source_unit =
        resolve_signal_contract_or_empty(signal_contracts, src);
    const std::string target_unit =
        resolve_signal_contract_or_empty(signal_contracts, tgt);

    if (strict && source_unit.empty()) {
      throw std::runtime_error(
          "GraphCompiler: strict mode requires declared source signal contract "
          "for edge[" +
          std::to_string(edge_index) + "] ('" + edge_spec.source_path +
          "' -> '" + edge_spec.target_path + "')");
    }
    if (strict && target_unit.empty()) {
      throw std::runtime_error(
          "GraphCompiler: strict mode requires declared target signal contract "
          "for edge[" +
          std::to_string(edge_index) + "] ('" + edge_spec.source_path +
          "' -> '" + edge_spec.target_path + "')");
    }

    TransformRegistryEntry transform_entry;
    {
      auto &registry = factory_registry();
      std::lock_guard<std::mutex> lock(registry.mutex);
      ensure_default_factories_registered_locked(registry);
      const auto &entry =
          resolve_transform_entry_or_throw(registry, edge_spec.transform.type);
      transform_entry = entry;
    }

    if (strict && !transform_entry.has_signature) {
      throw std::runtime_error(
          "GraphCompiler: strict mode requires signature metadata for "
          "transform type '" +
          edge_spec.transform.type + "' on edge['" + edge_spec.source_path +
          "' -> '" + edge_spec.target_path + "']");
    }

    TransformSpec resolved_transform_spec = edge_spec.transform;

    const bool both_declared = !source_unit.empty() && !target_unit.empty();
    const bool both_known = both_declared &&
                            is_unit_known(unit_registry, source_unit) &&
                            is_unit_known(unit_registry, target_unit);

    const TransformSignature::Contract contract =
        transform_entry.has_signature ? transform_entry.signature.contract
                                      : TransformSignature::Contract::preserve;

    if (contract == TransformSignature::Contract::unit_convert) {
      const std::string edge_context =
          "edge[" + std::to_string(edge_index) + "]";

      const std::string to_unit = as_string(
          require_param(edge_spec.transform.params, "to_unit", edge_context),
          edge_context + "/transform/params/to_unit");
      if (!is_unit_known(unit_registry, to_unit)) {
        const std::string message =
            "GraphCompiler: unit_convert unknown to_unit '" + to_unit +
            "' at " + edge_context;
        if (strict) {
          throw std::runtime_error(message);
        }
        emit_warning(options, message);
      }

      std::string from_assertion;
      if (auto it = edge_spec.transform.params.find("from_unit");
          it != edge_spec.transform.params.end()) {
        from_assertion =
            as_string(it->second, edge_context + "/transform/params/from_unit");
      }

      if (!from_assertion.empty() && !source_unit.empty() &&
          from_assertion != source_unit) {
        const std::string message =
            "GraphCompiler: unit_convert from_unit assertion '" +
            from_assertion + "' does not match declared source unit '" +
            source_unit + "' at " + edge_context;
        if (strict) {
          throw std::runtime_error(message);
        }
        emit_warning(options, message);
      }

      if (!target_unit.empty() && target_unit != to_unit) {
        const std::string message =
            "GraphCompiler: unit_convert to_unit '" + to_unit +
            "' does not match declared target unit '" + target_unit +
            "' on edge['" + edge_spec.source_path + "' -> '" +
            edge_spec.target_path + "']";
        if (strict) {
          throw std::runtime_error(message);
        }
        emit_warning(options, message);
      }

      std::string from_unit = source_unit;
      if (from_unit.empty()) {
        from_unit = from_assertion;
      }

      UnitConversion conversion;
      conversion.scale = 1.0;
      conversion.offset = 0.0;

      if (!from_unit.empty() && !to_unit.empty()) {
        try {
          conversion = unit_registry.resolve_conversion(from_unit, to_unit);
        } catch (const std::exception &e) {
          if (strict) {
            throw std::runtime_error(
                "GraphCompiler: unit_convert conversion resolution failed on "
                "edge['" +
                edge_spec.source_path + "' -> '" + edge_spec.target_path +
                "']: " + e.what());
          }
          emit_warning(options,
                       "GraphCompiler: permissive unit_convert conversion "
                       "resolution failed on edge['" +
                           edge_spec.source_path + "' -> '" +
                           edge_spec.target_path + "']: " + e.what());
        }
      }

      resolved_transform_spec.params["__resolved_scale"] = conversion.scale;
      resolved_transform_spec.params["__resolved_offset"] = conversion.offset;
    } else if (both_known) {
      if (contract == TransformSignature::Contract::linear_conditioning) {
        if (strict && source_unit != target_unit) {
          throw std::runtime_error(
              "GraphCompiler: strict mode disallows unit-boundary crossing via "
              "linear transform on edge['" +
              edge_spec.source_path + "' -> '" + edge_spec.target_path +
              "']; use unit_convert");
        }

        if (!strict && !has_compatible_dimension_and_kind(
                           unit_registry, source_unit, target_unit)) {
          emit_warning(options,
                       "GraphCompiler: permissive linear boundary warning on "
                       "edge['" +
                           edge_spec.source_path + "' -> '" +
                           edge_spec.target_path + "'] (source unit '" +
                           source_unit + "', target unit '" + target_unit +
                           "')");
        }
      } else {
        if (!has_compatible_dimension_and_kind(unit_registry, source_unit,
                                               target_unit)) {
          const std::string message =
              "GraphCompiler: incompatible unit contracts on edge['" +
              edge_spec.source_path + "' -> '" + edge_spec.target_path +
              "'] (source='" + source_unit + "', target='" + target_unit + "')";
          if (strict) {
            throw std::runtime_error(message);
          }
          emit_warning(options, message);
        }
      }
    } else if (!strict && both_declared &&
               contract == TransformSignature::Contract::linear_conditioning) {
      emit_warning(
          options,
          "GraphCompiler: permissive linear boundary warning could not "
          "fully validate units on edge['" +
              edge_spec.source_path + "' -> '" + edge_spec.target_path +
              "'] because one or both units are unknown to registry");
    }

    ITransform *tf = parse_transform(resolved_transform_spec);
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

  for (size_t model_index = 0; model_index < program.models.size();
       ++model_index) {
    const auto &model = program.models[model_index];
    const auto output_ids = model->output_signal_ids();
    for (SignalId output_id : output_ids) {
      if (output_id == INVALID_SIGNAL) {
        throw std::runtime_error(
            "Model output_signal_ids() returned INVALID_SIGNAL for model[" +
            std::to_string(model_index) + ":" + model->describe() + "]");
      }
      if (output_id >= signal_ns.size()) {
        throw std::runtime_error(
            "Model output_signal_ids() returned non-interned signal id " +
            std::to_string(output_id) + " for model[" +
            std::to_string(model_index) + ":" + model->describe() + "]");
      }
      register_writer(output_id, "model_output[" + std::to_string(model_index) +
                                     ":" + model->describe() + "]");
    }
  }

  detect_cycles(program.edges);
  topological_sort(program.edges);

  // Compile rules with threshold unit policy.
  for (const auto &rule_spec : spec.rules) {
    const std::string trimmed = trim_copy(rule_spec.condition);
    std::smatch match;
    if (std::regex_match(trimmed, match, rule_comparator_regex())) {
      const std::string signal_path = match[1].str();
      const SignalId signal_id = signal_ns.intern(signal_path);
      const std::string lhs_unit =
          resolve_signal_contract_or_empty(signal_contracts, signal_id);
      if (strict && lhs_unit.empty()) {
        throw std::runtime_error(
            "GraphCompiler: strict mode requires declared unit contract for "
            "rule LHS signal '" +
            signal_path + "' in rule '" + rule_spec.id + "'");
      }
      if (strict && !lhs_unit.empty() &&
          !is_unit_known(unit_registry, lhs_unit)) {
        throw std::runtime_error(
            "GraphCompiler: strict mode rule LHS signal '" + signal_path +
            "' uses unknown unit symbol '" + lhs_unit + "'");
      }
    }

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

  for (const auto &[id, unit] : signal_contracts) {
    program.signal_unit_contracts.emplace_back(id, unit);
  }
  std::sort(
      program.signal_unit_contracts.begin(),
      program.signal_unit_contracts.end(),
      [](const auto &lhs, const auto &rhs) { return lhs.first < rhs.first; });

  program.required_signal_capacity = signal_ns.size();

  for (const auto &rule : program.rules) {
    program.required_command_capacity += rule.device_functions.size();
  }

  return program;
}

ITransform *GraphCompiler::parse_transform(const TransformSpec &spec) {
  TransformFactory factory;
  {
    auto &registry = factory_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    ensure_default_factories_registered_locked(registry);

    const auto &entry = resolve_transform_entry_or_throw(registry, spec.type);
    factory = entry.factory;
  }

  auto transform = factory(spec);
  if (!transform) {
    throw std::runtime_error("Transform factory returned null for type '" +
                             spec.type + "'");
  }
  return transform.release();
}

IModel *GraphCompiler::parse_model(const ModelSpec &spec, SignalNamespace &ns) {
  ModelFactory factory;
  {
    auto &registry = factory_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    ensure_default_factories_registered_locked(registry);

    const auto &entry = resolve_model_entry_or_throw(registry, spec.type);
    factory = entry.factory;
  }

  auto model = factory(spec, ns);
  if (!model) {
    throw std::runtime_error("Model factory returned null for type '" +
                             spec.type + "'");
  }
  return model.release();
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

  std::map<SignalId, std::vector<size_t>> outgoing;
  std::map<SignalId, int> in_degree;
  std::set<SignalId> all_signals;

  for (size_t idx : immediate_indices) {
    all_signals.insert(edges[idx].source);
    all_signals.insert(edges[idx].target);
    outgoing[edges[idx].source].push_back(idx);
    in_degree[edges[idx].target]++;
  }

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

  for (size_t idx : delay_indices) {
    sorted.push_back(std::move(edges[idx]));
  }

  for (size_t idx : sorted_immediate_indices) {
    sorted.push_back(std::move(edges[idx]));
  }

  edges = std::move(sorted);
}

void GraphCompiler::detect_cycles(const std::vector<CompiledEdge> &edges) {
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

  std::map<SignalId, int> state;
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
