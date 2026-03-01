#include "fluxgraph/core/signal_store.hpp"
#include <algorithm>
#include <stdexcept>

namespace fluxgraph {

SignalStore::SignalStore() = default;
SignalStore::~SignalStore() = default;

const std::string &SignalStore::dimensionless_unit() {
  static const std::string kDimensionless = "dimensionless";
  return kDimensionless;
}

void SignalStore::ensure_index(SignalId id) {
  const size_t needed = static_cast<size_t>(id) + 1U;
  if (needed <= signals_.size()) {
    return;
  }

  signals_.resize(needed);
  has_signal_.resize(needed, static_cast<uint8_t>(0));
  physics_driven_.resize(needed, static_cast<uint8_t>(0));
  declared_units_.resize(needed);
  has_declared_unit_.resize(needed, static_cast<uint8_t>(0));
}

void SignalStore::write(SignalId id, double value, const std::string &unit) {
  if (id == INVALID_SIGNAL) {
    return; // Silently ignore invalid IDs
  }

  ensure_index(id);
  const size_t index = static_cast<size_t>(id);
  const std::string &normalized_unit = unit.empty() ? dimensionless_unit() : unit;

  // First non-dimensionless write declares expected unit if none is declared.
  // This avoids accidentally freezing unit contracts to "dimensionless" when a
  // signal is still in its unwritten/default state.
  if (!has_declared_unit_[index] && normalized_unit != dimensionless_unit()) {
    declared_units_[index] = normalized_unit;
    has_declared_unit_[index] = static_cast<uint8_t>(1);
  }

  // Validate unit if declared
  if (has_declared_unit_[index] && declared_units_[index] != normalized_unit) {
    throw std::runtime_error("Unit mismatch for signal " + std::to_string(id) +
                             ": expected '" + declared_units_[index] + "', got '" +
                             normalized_unit + "'");
  }

  if (!has_signal_[index]) {
    has_signal_[index] = static_cast<uint8_t>(1);
    ++signal_count_;
  }

  signals_[index].value = value;
  if (signals_[index].unit != normalized_unit) {
    signals_[index].unit = normalized_unit;
  }
}

Signal SignalStore::read(SignalId id) const {
  if (id == INVALID_SIGNAL) {
    return Signal(); // Return default signal
  }

  const size_t index = static_cast<size_t>(id);
  if (index >= signals_.size() || !has_signal_[index]) {
    return Signal(); // Return default if not found
  }

  return signals_[index];
}

double SignalStore::read_value(SignalId id) const {
  if (id == INVALID_SIGNAL) {
    return 0.0;
  }

  const size_t index = static_cast<size_t>(id);
  if (index >= signals_.size() || !has_signal_[index]) {
    return 0.0;
  }

  return signals_[index].value;
}

bool SignalStore::is_physics_driven(SignalId id) const {
  if (id == INVALID_SIGNAL) {
    return false;
  }

  const size_t index = static_cast<size_t>(id);
  return index < physics_driven_.size() && physics_driven_[index] != 0;
}

void SignalStore::mark_physics_driven(SignalId id, bool driven) {
  if (id == INVALID_SIGNAL) {
    return;
  }

  const size_t index = static_cast<size_t>(id);
  if (driven) {
    ensure_index(id);
    physics_driven_[index] = static_cast<uint8_t>(1);
    return;
  }

  if (index < physics_driven_.size()) {
    physics_driven_[index] = static_cast<uint8_t>(0);
  }
}

void SignalStore::declare_unit(SignalId id, const std::string &expected_unit) {
  if (id == INVALID_SIGNAL) {
    return;
  }

  ensure_index(id);
  const size_t index = static_cast<size_t>(id);
  declared_units_[index] = expected_unit;
  has_declared_unit_[index] = static_cast<uint8_t>(1);
}

void SignalStore::validate_unit(SignalId id, const std::string &unit) const {
  if (id == INVALID_SIGNAL) {
    return;
  }

  const size_t index = static_cast<size_t>(id);
  if (index >= has_declared_unit_.size() || !has_declared_unit_[index]) {
    return;
  }

  const std::string &normalized_unit = unit.empty() ? dimensionless_unit() : unit;
  if (declared_units_[index] != normalized_unit) {
    throw std::runtime_error("Unit mismatch for signal " + std::to_string(id) +
                             ": expected '" + declared_units_[index] + "', got '" +
                             normalized_unit + "'");
  }
}

void SignalStore::reserve(size_t max_signals) {
  signals_.reserve(max_signals);
  has_signal_.reserve(max_signals);
  physics_driven_.reserve(max_signals);
  declared_units_.reserve(max_signals);
  has_declared_unit_.reserve(max_signals);

  if (max_signals > signals_.size()) {
    signals_.resize(max_signals);
    has_signal_.resize(max_signals, static_cast<uint8_t>(0));
    physics_driven_.resize(max_signals, static_cast<uint8_t>(0));
    declared_units_.resize(max_signals);
    has_declared_unit_.resize(max_signals, static_cast<uint8_t>(0));
  }
}

size_t SignalStore::capacity() const { return signals_.size(); }

size_t SignalStore::size() const { return signal_count_; }

void SignalStore::clear() {
  std::fill(has_signal_.begin(), has_signal_.end(), static_cast<uint8_t>(0));
  std::fill(physics_driven_.begin(), physics_driven_.end(), static_cast<uint8_t>(0));
  signal_count_ = 0;

  // Note: We keep declared_units_ as they are part of the graph structure
}

} // namespace fluxgraph
