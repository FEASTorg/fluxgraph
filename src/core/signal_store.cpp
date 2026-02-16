#include "fluxgraph/core/signal_store.hpp"
#include <stdexcept>

namespace fluxgraph {

SignalStore::SignalStore() = default;
SignalStore::~SignalStore() = default;

void SignalStore::write(SignalId id, double value, const std::string& unit) {
    if (id == INVALID_SIGNAL) {
        return; // Silently ignore invalid IDs
    }

    // Validate unit if declared
    auto it = declared_units_.find(id);
    if (it != declared_units_.end() && it->second != unit) {
        throw std::runtime_error("Unit mismatch for signal " + std::to_string(id) +
                                 ": expected '" + it->second + "', got '" + unit + "'");
    }

    signals_[id] = Signal(value, unit);
}

Signal SignalStore::read(SignalId id) const {
    if (id == INVALID_SIGNAL) {
        return Signal(); // Return default signal
    }

    auto it = signals_.find(id);
    if (it != signals_.end()) {
        return it->second;
    }
    return Signal(); // Return default if not found
}

double SignalStore::read_value(SignalId id) const {
    return read(id).value;
}

bool SignalStore::is_physics_driven(SignalId id) const {
    return physics_driven_.count(id) > 0;
}

void SignalStore::mark_physics_driven(SignalId id, bool driven) {
    if (driven) {
        physics_driven_.insert(id);
    } else {
        physics_driven_.erase(id);
    }
}

void SignalStore::declare_unit(SignalId id, const std::string& expected_unit) {
    declared_units_[id] = expected_unit;
}

void SignalStore::validate_unit(SignalId id, const std::string& unit) const {
    auto it = declared_units_.find(id);
    if (it != declared_units_.end() && it->second != unit) {
        throw std::runtime_error("Unit mismatch for signal " + std::to_string(id) +
                                 ": expected '" + it->second + "', got '" + unit + "'");
    }
}

void SignalStore::reserve(size_t max_signals) {
    (void)max_signals; // Unused parameter (std::map doesn't support reserve)
    // std::map doesn't have reserve, but we can document expected capacity
    // This is a no-op for now, but useful for future optimization
}

size_t SignalStore::capacity() const {
    return signals_.size(); // For map, capacity == size
}

size_t SignalStore::size() const {
    return signals_.size();
}

void SignalStore::clear() {
    signals_.clear();
    physics_driven_.clear();
    // Note: We keep declared_units_ as they are part of the graph structure
}

} // namespace fluxgraph
