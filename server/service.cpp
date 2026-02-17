#include "service.hpp"
#include "fluxgraph/loaders/yaml_loader.hpp"
#include "fluxgraph/loaders/json_loader.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <random>

namespace fluxgraph::server {

// ============================================================================
// Constructor / Destructor
// ============================================================================

FluxGraphServiceImpl::FluxGraphServiceImpl() {
    std::cout << "[FluxGraph] Service initialized\n";
}

FluxGraphServiceImpl::~FluxGraphServiceImpl() {
    std::cout << "[FluxGraph] Service shutdown\n";
}

// ============================================================================
// LoadConfig RPC
// ============================================================================

grpc::Status FluxGraphServiceImpl::LoadConfig(
    grpc::ServerContext* context,
    const fluxgraph::rpc::ConfigRequest* request,
    fluxgraph::rpc::ConfigResponse* response) {

    std::lock_guard lock(state_mutex_);

    try {
        // Check for no-op (matching hash)
        if (!request->config_hash().empty() &&
            request->config_hash() == current_config_hash_) {
            response->set_success(true);
            response->set_config_changed(false);
            std::cout << "[FluxGraph] LoadConfig: no-op (hash matched)\n";
            return grpc::Status::OK;
        }

        // Parse config based on format
        GraphSpec spec;
        if (request->format() == "yaml") {
#ifdef FLUXGRAPH_YAML_ENABLED
            spec = fluxgraph::loaders::load_yaml_string(request->config_content());
#else
            response->set_success(false);
            response->set_error_message("YAML support not enabled (build with -DFLUXGRAPH_YAML_ENABLED=ON)");
            return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                              "YAML support not enabled");
#endif
        } else if (request->format() == "json") {
#ifdef FLUXGRAPH_JSON_ENABLED
            spec = fluxgraph::loaders::load_json_string(request->config_content());
#else
            response->set_success(false);
            response->set_error_message("JSON support not enabled (build with -DFLUXGRAPH_JSON_ENABLED=ON)");
            return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                              "JSON support not enabled");
#endif
        } else {
            response->set_success(false);
            response->set_error_message("Unknown format: " + request->format() +
                                      " (must be 'yaml' or 'json')");
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "Unknown format");
        }

        // Clear existing namespaces (fresh start)
        signal_ns_.clear();
        func_ns_.clear();

        // Compile graph
        GraphCompiler compiler;
        auto program = compiler.compile(spec, signal_ns_, func_ns_);

        // Load into engine
        engine_.load(std::move(program));

        // Reset simulation state
        store_.clear();
        sim_time_ = 0.0;
        sessions_.clear();

        // Update config hash
        current_config_hash_ = request->config_hash();
        loaded_ = true;

        response->set_success(true);
        response->set_config_changed(true);

        std::cout << "[FluxGraph] Config loaded: "
                  << spec.models.size() << " models, "
                  << spec.edges.size() << " edges, "
                  << spec.rules.size() << " rules\n";

        return grpc::Status::OK;

    } catch (const std::exception& e) {
        response->set_success(false);
        response->set_error_message(e.what());
        std::cerr << "[FluxGraph] LoadConfig failed: " << e.what() << "\n";
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, e.what());
    }
}

// ============================================================================
// RegisterProvider RPC
// ============================================================================

grpc::Status FluxGraphServiceImpl::RegisterProvider(
    grpc::ServerContext* context,
    const fluxgraph::rpc::ProviderRegistration* request,
    fluxgraph::rpc::ProviderRegistrationResponse* response) {

    std::lock_guard lock(state_mutex_);

    if (!loaded_) {
        response->set_success(false);
        response->set_error_message("Config not loaded - call LoadConfig first");
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                          "Config not loaded");
    }

    // Generate unique session ID
    std::string session_id = generate_session_id(request->provider_id());

    // Store provider session
    ProviderSession session;
    session.provider_id = request->provider_id();
    session.device_ids.assign(request->device_ids().begin(),
                             request->device_ids().end());
    session.last_update = std::chrono::steady_clock::now();
    session.updated_this_tick = false;

    sessions_[session_id] = session;

    response->set_success(true);
    response->set_session_id(session_id);

    std::cout << "[FluxGraph] Provider registered: "
              << request->provider_id()
              << " (session: " << session_id << ")\n";

    return grpc::Status::OK;
}

// ============================================================================
// UpdateSignals RPC (Server-Driven Tick)
// ============================================================================

grpc::Status FluxGraphServiceImpl::UpdateSignals(
    grpc::ServerContext* context,
    const fluxgraph::rpc::SignalUpdates* request,
    fluxgraph::rpc::TickResponse* response) {

    std::lock_guard lock(state_mutex_);

    if (!loaded_) {
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                          "Config not loaded");
    }

    // Validate session
    auto session_it = sessions_.find(request->session_id());
    if (session_it == sessions_.end()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                          "Invalid session_id - call RegisterProvider first");
    }

    // Update signals from provider
    for (const auto& sig : request->signals()) {
        SignalId id = signal_ns_.resolve(sig.path());
        if (id == INVALID_SIGNAL) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "Unknown signal: " + sig.path());
        }
        store_.write(id, sig.value(), sig.unit().c_str());
    }

    // Mark provider as updated
    session_it->second.last_update = std::chrono::steady_clock::now();
    session_it->second.updated_this_tick = true;

    // Check if all registered providers have updated
    bool all_ready = all_providers_updated();

    if (all_ready && !sessions_.empty()) {
        // Execute physics tick
        engine_.tick(dt_, store_);
        sim_time_ += dt_;

        // Reset update tracking for next tick
        reset_update_flags();

        // Get all commands from rules
        auto all_commands = engine_.drain_commands();

        // Filter commands for this provider
        const std::string& provider_id = session_it->second.provider_id;
        auto provider_commands = filter_commands_for_provider(provider_id, all_commands);

        // Build response
        response->set_tick_occurred(true);
        response->set_sim_time_sec(sim_time_);

        for (const auto& cmd : provider_commands) {
            auto* pb_cmd = response->add_commands();
            convert_command(cmd, pb_cmd);
        }

        std::cout << "[FluxGraph] Tick " << static_cast<int>(sim_time_ / dt_)
                  << " (t=" << std::fixed << std::setprecision(3) << sim_time_
                  << "s, commands=" << provider_commands.size() << ")\n";

    } else {
        // Not all providers ready yet
        response->set_tick_occurred(false);
        response->set_sim_time_sec(sim_time_);
    }

    return grpc::Status::OK;
}

// ============================================================================
// ReadSignals RPC
// ============================================================================

grpc::Status FluxGraphServiceImpl::ReadSignals(
    grpc::ServerContext* context,
    const fluxgraph::rpc::SignalRequest* request,
    fluxgraph::rpc::SignalResponse* response) {

    std::lock_guard lock(state_mutex_);

    if (!loaded_) {
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                          "Config not loaded");
    }

    for (const auto& path : request->paths()) {
        SignalId id = signal_ns_.resolve(path);
        if (id == INVALID_SIGNAL) {
            // Skip unknown signals (or could return error)
            continue;
        }

        auto signal = store_.read(id);
        auto* val = response->add_signals();
        val->set_path(path);
        val->set_value(signal.value);
        val->set_unit(signal.unit);
        val->set_physics_driven(store_.is_physics_driven(id));
    }

    return grpc::Status::OK;
}

// ============================================================================
// Reset RPC
// ============================================================================

grpc::Status FluxGraphServiceImpl::Reset(
    grpc::ServerContext* context,
    const fluxgraph::rpc::ResetRequest* request,
    fluxgraph::rpc::ResetResponse* response) {

    std::lock_guard lock(state_mutex_);

    if (!loaded_) {
        response->set_success(false);
        response->set_error_message("Config not loaded");
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                          "Config not loaded");
    }

    try {
        // Reset engine state
        engine_.reset();
        store_.clear();
        sim_time_ = 0.0;

        // Reset provider update tracking
        reset_update_flags();

        response->set_success(true);
        std::cout << "[FluxGraph] Reset complete\n";

        return grpc::Status::OK;

    } catch (const std::exception& e) {
        response->set_success(false);
        response->set_error_message(e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}

// ============================================================================
// Check RPC (Health Check)
// ============================================================================

grpc::Status FluxGraphServiceImpl::Check(
    grpc::ServerContext* context,
    const fluxgraph::rpc::HealthCheckRequest* request,
    fluxgraph::rpc::HealthCheckResponse* response) {

    // Simple health check: If service is running, it's healthy
    if (request->service().empty() || request->service() == "fluxgraph") {
        response->set_status(fluxgraph::rpc::HealthCheckResponse::SERVING);
    } else {
        response->set_status(fluxgraph::rpc::HealthCheckResponse::SERVICE_UNKNOWN);
    }

    return grpc::Status::OK;
}

// ============================================================================
// Helper Methods
// ============================================================================

std::string FluxGraphServiceImpl::generate_session_id(const std::string& provider_id) {
    // Generate unique session ID: provider_id + timestamp + random
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);

    std::ostringstream oss;
    oss << provider_id << "_" << timestamp << "_" << dis(gen);
    return oss.str();
}

bool FluxGraphServiceImpl::all_providers_updated() const {
    if (sessions_.empty()) {
        return false;
    }

    for (const auto& [session_id, session] : sessions_) {
        if (!session.updated_this_tick) {
            return false;
        }
    }

    return true;
}

void FluxGraphServiceImpl::reset_update_flags() {
    for (auto& [session_id, session] : sessions_) {
        session.updated_this_tick = false;
    }
}

void FluxGraphServiceImpl::convert_command(
    const fluxgraph::Command& cmd,
    fluxgraph::rpc::Command* pb_cmd) {

    pb_cmd->set_device(func_ns_.lookup_device(cmd.device));
    pb_cmd->set_function(func_ns_.lookup_function(cmd.function));

    for (const auto& [key, variant] : cmd.args) {
        auto& arg = (*pb_cmd->mutable_args())[key];

        if (std::holds_alternative<double>(variant)) {
            arg.set_double_val(std::get<double>(variant));
        } else if (std::holds_alternative<int64_t>(variant)) {
            arg.set_int_val(std::get<int64_t>(variant));
        } else if (std::holds_alternative<bool>(variant)) {
            arg.set_bool_val(std::get<bool>(variant));
        } else if (std::holds_alternative<std::string>(variant)) {
            arg.set_string_val(std::get<std::string>(variant));
        }
    }
}

std::vector<fluxgraph::Command> FluxGraphServiceImpl::filter_commands_for_provider(
    const std::string& provider_id,
    const std::vector<fluxgraph::Command>& all_commands) {

    // Find provider's devices
    std::vector<std::string> device_ids;
    for (const auto& [session_id, session] : sessions_) {
        if (session.provider_id == provider_id) {
            device_ids = session.device_ids;
            break;
        }
    }

    if (device_ids.empty()) {
        return {};
    }

    // Filter commands targeting this provider's devices
    std::vector<fluxgraph::Command> filtered;
    for (const auto& cmd : all_commands) {
        std::string device_name = func_ns_.lookup_device(cmd.device);
        
        for (const auto& owned_device : device_ids) {
            if (device_name == owned_device) {
                filtered.push_back(cmd);
                break;
            }
        }
    }

    return filtered;
}

} // namespace fluxgraph::server
