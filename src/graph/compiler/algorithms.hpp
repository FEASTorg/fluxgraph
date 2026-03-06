#pragma once

#include "fluxgraph/graph/compiler.hpp"
#include <memory>
#include <vector>

namespace fluxgraph::compiler_internal {

void topological_sort_edges(std::vector<CompiledEdge> &edges);
void detect_cycles_in_non_delay_subgraph(const std::vector<CompiledEdge> &edges);
void validate_model_stability_limits(
    const std::vector<std::unique_ptr<IModel>> &models, double expected_dt);

} // namespace fluxgraph::compiler_internal
