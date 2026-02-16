#pragma once

#include "fluxgraph/core/types.hpp"
#include <map>
#include <set>
#include <string>

namespace fluxgraph {

/// Represents a signal with its value and unit metadata
struct Signal {
    double value = 0.0;
    std::string unit = "dimensionless";

    Signal() = default;
    Signal(double v, const std::string& u = "dimensionless") 
        : value(v), unit(u) {}
};

/// Central storage for all signal values and metadata
/// Single-writer by design - no internal synchronization
class SignalStore {
public:
    SignalStore();
    ~SignalStore();

    /// Write a signal value with unit metadata
    void write(SignalId id, double value, const std::string& unit = "dimensionless");

    /// Read a signal (value + unit)
    Signal read(SignalId id) const;

    /// Read only the value (convenience method)
    double read_value(SignalId id) const;

    /// Check if a signal is driven by physics simulation
    bool is_physics_driven(SignalId id) const;

    /// Mark a signal as physics-driven (set by graph compilation)
    void mark_physics_driven(SignalId id, bool driven);

    /// Declare expected unit for a signal (enforced on write)
    void declare_unit(SignalId id, const std::string& expected_unit);

    /// Validate that a unit matches the declared unit for a signal
    /// Throws std::runtime_error if mismatch
    void validate_unit(SignalId id, const std::string& unit) const;

    /// Pre-allocate storage for signals (optimization)
    void reserve(size_t max_signals);

    /// Get current capacity
    size_t capacity() const;

    /// Get number of signals currently stored
    size_t size() const;

    /// Clear all signals
    void clear();

private:
    std::map<SignalId, Signal> signals_;
    std::set<SignalId> physics_driven_;
    std::map<SignalId, std::string> declared_units_;
};

} // namespace fluxgraph
