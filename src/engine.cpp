#include "fluxgraph/engine.hpp"
#include <stdexcept>

namespace fluxgraph {

Engine::Engine() : loaded_(false) {}

Engine::~Engine() = default;

void Engine::load(CompiledProgram program) {
  edges_ = std::move(program.edges);
  models_ = std::move(program.models);
  rules_ = std::move(program.rules);
  loaded_ = true;
}

void Engine::tick(double dt, SignalStore &store) {
  if (!loaded_) {
    throw std::runtime_error("Engine: No program loaded");
  }
  if (dt <= 0.0) {
    throw std::runtime_error("Engine: dt must be positive");
  }

  // Runtime stability contract: enforce model limits for supplied dt.
  for (const auto &model : models_) {
    const double limit = model->compute_stability_limit();
    if (dt > limit) {
      throw std::runtime_error("Engine: stability violation for model '" +
                               model->describe() +
                               "' (dt=" + std::to_string(dt) +
                               " exceeds limit=" + std::to_string(limit) + ")");
    }
  }

  // Stage 1: Input boundary freeze
  // (external writes are assumed complete before tick entry)

  // Stage 2: Update physics models
  update_models(dt, store);

  // Stage 3: Apply transforms in topological order with immediate propagation
  process_edges(dt, store);

  // Stage 4: Commit outputs (future: validation, dirty flags)
  commit_outputs(store);

  // Stage 5: Evaluate rules and emit commands
  evaluate_rules(store);
}

std::vector<Command> Engine::drain_commands() {
  std::vector<Command> drained = std::move(command_queue_);
  command_queue_.clear();
  return drained;
}

void Engine::reset() {
  // Reset all models
  for (auto &model : models_) {
    model->reset();
  }

  // Reset all transforms
  for (auto &edge : edges_) {
    edge.transform->reset();
  }

  // Clear command queue
  command_queue_.clear();
}

void Engine::process_edges(double dt, SignalStore &store) {
  for (auto &edge : edges_) {
    const auto source = store.read(edge.source);
    double output = edge.transform->apply(source.value, dt);
    store.write(edge.target, output, source.unit);
  }
}

void Engine::update_models(double dt, SignalStore &store) {
  for (auto &model : models_) {
    model->tick(dt, store);
  }
}

void Engine::commit_outputs(SignalStore &store) {
  // Future: Signal validation, dirty flag clearing, etc.
  // For now, this is a no-op
  (void)store;
}

void Engine::evaluate_rules(SignalStore &store) {
  for (const auto &rule : rules_) {
    if (rule.condition(store)) {
      // Emit commands for all actions
      for (size_t i = 0; i < rule.device_functions.size(); ++i) {
        Command cmd;
        cmd.device = rule.device_functions[i].first;
        cmd.function = rule.device_functions[i].second;
        cmd.args = rule.args_list[i];
        command_queue_.push_back(std::move(cmd));
      }
    }
  }
}

} // namespace fluxgraph
