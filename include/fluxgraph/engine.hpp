#pragma once

#include "fluxgraph/command.hpp"
#include "fluxgraph/graph/compiler.hpp"
#include <vector>

namespace fluxgraph {

/// Main simulation engine with five-stage tick execution
/// Execution model:
/// 1. Input boundary freeze (external writes before tick begin)
/// 2. Models: Update physics models
/// 3. Edges: Apply transforms in topological order (immediate propagation)
/// 4. Commit: Finalize signal values
/// 5. Rules: Evaluate conditions and emit commands
class Engine {
public:
  Engine();
  ~Engine();

  /// Load a compiled program into the engine
  /// @param program Compiled graph program
  void load(CompiledProgram program);

  /// Execute one simulation tick
  /// @param dt Time step in seconds
  /// @param store Signal storage
  void tick(double dt, SignalStore &store);

  /// Drain queued commands (for external processing)
  /// @return All commands generated since last drain
  std::vector<Command> drain_commands();

  /// Reset all models and transforms to initial state
  void reset();

  /// Check if a program is loaded
  bool is_loaded() const { return loaded_; }

private:
  bool loaded_;
  std::vector<CompiledEdge> edges_;
  std::vector<std::unique_ptr<IModel>> models_;
  std::vector<CompiledRule> rules_;
  std::vector<Command> command_queue_;

  // Five-stage tick implementation
  void process_edges(double dt, SignalStore &store);
  void update_models(double dt, SignalStore &store);
  void commit_outputs(SignalStore &store);
  void evaluate_rules(SignalStore &store);
};

} // namespace fluxgraph
