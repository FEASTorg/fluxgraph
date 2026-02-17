#pragma once

#include <grpcpp/grpcpp.h>
#include "fluxgraph.grpc.pb.h"
#include "fluxgraph/engine.hpp"
#include "fluxgraph/command.hpp"
#include "fluxgraph/core/signal_store.hpp"
#include "fluxgraph/core/namespace.hpp"
#include "fluxgraph/graph/compiler.hpp"
#include <mutex>
#include <map>
#include <chrono>
#include <string>

namespace fluxgraph::server {

/// Provider session information
struct ProviderSession {
    std::string provider_id;
    std::vector<std::string> device_ids;
    std::chrono::steady_clock::time_point last_update;
    bool updated_this_tick = false;
};

/// FluxGraph gRPC service implementation
/// 
/// Thread-safety: All RPC handlers are serialized with a single mutex.
/// This is the simplest correct approach for Phase 24.
/// 
/// Tick coordination: Server waits for all registered providers to call
/// UpdateSignals, then advances physics simulation once per tick.
class FluxGraphServiceImpl final : public fluxgraph::rpc::FluxGraph::Service {
public:
    FluxGraphServiceImpl();
    ~FluxGraphServiceImpl() override;

    // ========================================================================
    // RPC Handlers
    // ========================================================================

    grpc::Status LoadConfig(
        grpc::ServerContext* context,
        const fluxgraph::rpc::ConfigRequest* request,
        fluxgraph::rpc::ConfigResponse* response) override;

    grpc::Status RegisterProvider(
        grpc::ServerContext* context,
        const fluxgraph::rpc::ProviderRegistration* request,
        fluxgraph::rpc::ProviderRegistrationResponse* response) override;

    grpc::Status UpdateSignals(
        grpc::ServerContext* context,
        const fluxgraph::rpc::SignalUpdates* request,
        fluxgraph::rpc::TickResponse* response) override;

    grpc::Status ReadSignals(
        grpc::ServerContext* context,
        const fluxgraph::rpc::SignalRequest* request,
        fluxgraph::rpc::SignalResponse* response) override;

    grpc::Status Reset(
        grpc::ServerContext* context,
        const fluxgraph::rpc::ResetRequest* request,
        fluxgraph::rpc::ResetResponse* response) override;

    grpc::Status Check(
        grpc::ServerContext* context,
        const fluxgraph::rpc::HealthCheckRequest* request,
        fluxgraph::rpc::HealthCheckResponse* response) override;

private:
    // ========================================================================
    // Core State
    // ========================================================================

    fluxgraph::Engine engine_;
    fluxgraph::SignalStore store_;
    fluxgraph::SignalNamespace signal_ns_;
    fluxgraph::FunctionNamespace func_ns_;

    // Thread safety
    std::mutex state_mutex_;

    // Configuration
    bool loaded_ = false;
    std::string current_config_hash_;
    double dt_ = 0.1;  // Default timestep (10Hz)
    double sim_time_ = 0.0;

    // Provider tracking
    std::map<std::string, ProviderSession> sessions_;  // session_id -> session

    // ========================================================================
    // Helper Methods
    // ========================================================================

    // Generate unique session ID for provider
    std::string generate_session_id(const std::string& provider_id);

    // Check if all registered providers have updated this tick
    bool all_providers_updated() const;

    // Reset provider update flags for next tick
    void reset_update_flags();

    // Convert FluxGraph Command to protobuf Command
    void convert_command(
        const fluxgraph::Command& cmd,
        fluxgraph::rpc::Command* pb_cmd);

    // Filter commands for specific provider
    std::vector<fluxgraph::Command> filter_commands_for_provider(
        const std::string& provider_id,
        const std::vector<fluxgraph::Command>& all_commands);
};

} // namespace fluxgraph::server
