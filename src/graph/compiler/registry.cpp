#include "registry.hpp"
#include "common.hpp"
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
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace fluxgraph::compiler_internal {

namespace {

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

} // namespace

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

const ModelRegistryEntry &resolve_model_entry_or_throw(FactoryRegistry &registry,
                                                        const std::string &type) {
  auto it = registry.model_factories.find(type);
  if (it == registry.model_factories.end()) {
    throw std::runtime_error("Unknown model type: " + type);
  }
  return it->second;
}

} // namespace fluxgraph::compiler_internal
