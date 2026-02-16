#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace fluxgraph {

/// Unique identifier for a signal in the graph
using SignalId = uint32_t;

/// Unique identifier for a device
using DeviceId = uint32_t;

/// Unique identifier for a function/command
using FunctionId = uint32_t;

/// Sentinel value for invalid signal ID
constexpr SignalId INVALID_SIGNAL = 0xFFFFFFFF;

/// Sentinel value for invalid device ID
constexpr DeviceId INVALID_DEVICE = 0xFFFFFFFF;

/// Sentinel value for invalid function ID
constexpr FunctionId INVALID_FUNCTION = 0xFFFFFFFF;

/// Variant type for command arguments and signal values
using Variant = std::variant<double, int64_t, bool, std::string>;

} // namespace fluxgraph
