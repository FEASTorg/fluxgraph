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

  // EXECUTION MODEL: Snapshot-based with topologically-ordered edges
  // - All edge source values are cached at tick start (prevents algebraic
  // loops)
  // - Edges are evaluated in topological order (ensures determinism)
  // - Within-tick signal propagation is NOT supported (use explicit delays)

  // Stage 1: Snapshot all edge source values
  snapshot_inputs(store);

  // Stage 2: Apply transforms using snapshots, in topological order
  process_edges(dt, store);

  // Stage 3: Update physics models
  update_models(dt, store);

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
    edge.snapshot = 0.0;
  }

  // Clear command queue
  command_queue_.clear();
}

void Engine::snapshot_inputs(SignalStore &store) {
  for (auto &edge : edges_) {
    edge.snapshot = store.read_value(edge.source);
  }
}

void Engine::process_edges(double dt, SignalStore &store) {
  for (auto &edge : edges_) {
    // Apply transform to snapshot value
    double output = edge.transform->apply(edge.snapshot, dt);

    // Write to target signal (unit unknown at this stage, use dimensionless)
    store.write(edge.target, output, "dimensionless");
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
