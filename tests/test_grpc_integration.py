#!/usr/bin/env python3
"""Integration test for FluxGraph gRPC server.

Tests:
1. Health check
2. Config loading (YAML)
3. Provider registration
4. Signal updates and tick coordination
5. Signal reading
6. Reset
"""

import grpc
import time
import sys
from pathlib import Path

# Import generated protobuf code
import fluxgraph_pb2 as pb
import fluxgraph_pb2_grpc as pb_grpc


def test_health_check(stub):
    """Test health check RPC."""
    print("\n[TEST] Health Check")
    request = pb.HealthCheckRequest(service="fluxgraph")
    response = stub.Check(request)
    assert response.status == pb.HealthCheckResponse.SERVING, "Server not serving"
    print("✓ Server is healthy (SERVING)")


def test_load_config(stub, config_path):
    """Test loading a YAML config."""
    print(f"\n[TEST] Load Config: {config_path}")
    
    with open(config_path, 'r') as f:
        config_content = f.read()
    
    request = pb.ConfigRequest(
        config_content=config_content,
        format="yaml",
        config_hash="test_config_v1"
    )
    
    response = stub.LoadConfig(request)
    assert response.success, f"Config load failed: {response.error_message}"
    assert response.config_changed, "Config should have changed"
    print(f"✓ Config loaded successfully")
    
    # Test idempotency (same hash should not reload)
    response2 = stub.LoadConfig(request)
    assert response2.success, "Second load should succeed"
    assert not response2.config_changed, "Config should not have changed (same hash)"
    print("✓ Config idempotency verified (hash check works)")


def test_load_config_with_hash(stub, config_path, config_hash):
    """Test loading a YAML config with specific hash."""
    print(f"\n[TEST] Load Config: {config_path} (hash: {config_hash})")
    
    with open(config_path, 'r') as f:
        config_content = f.read()
    
    request = pb.ConfigRequest(
        config_content=config_content,
        format="yaml",
        config_hash=config_hash
    )
    
    response = stub.LoadConfig(request)
    assert response.success, f"Config load failed: {response.error_message}"
    print(f"✓ Config loaded successfully")


def test_register_provider(stub, provider_id, device_ids):
    """Test provider registration."""
    print(f"\n[TEST] Register Provider: {provider_id}")
    
    request = pb.ProviderRegistration(
        provider_id=provider_id,
        device_ids=device_ids
    )
    
    response = stub.RegisterProvider(request)
    assert response.success, f"Registration failed: {response.error_message}"
    assert response.session_id, "Session ID should not be empty"
    
    print(f"✓ Provider registered (session: {response.session_id})")
    return response.session_id


def test_single_provider_tick(stub, session_id):
    """Test tick with single provider."""
    print(f"\n[TEST] Single Provider Tick")
    
    # Send signal updates
    request = pb.SignalUpdates(
        session_id=session_id,
        signals=[
            pb.SignalUpdate(path="heater.output", value=500.0, unit="W"),
            pb.SignalUpdate(path="ambient.temp", value=25.0, unit="degC"),
        ]
    )
    
    response = stub.UpdateSignals(request)
    
    # With only one provider, tick should occur immediately
    assert response.tick_occurred, "Tick should have occurred"
    assert response.sim_time_sec >= 0.0, "Sim time should be valid"
    
    print(f"✓ Tick occurred (t={response.sim_time_sec:.3f}s, commands={len(response.commands)})")


def test_multi_provider_coordination(stub):
    """Test tick coordination with multiple providers."""
    print(f"\n[TEST] Multi-Provider Tick Coordination")
    
    # Register two providers
    session1 = test_register_provider(stub, "provider-A", ["heater"])
    session2 = test_register_provider(stub, "provider-B", ["sensor"])
    
    # Provider A updates first
    request1 = pb.SignalUpdates(
        session_id=session1,
        signals=[pb.SignalUpdate(path="heater.output", value=750.0, unit="W")]
    )
    response1 = stub.UpdateSignals(request1)
    
    # Tick should NOT occur yet (waiting for provider B)
    assert not response1.tick_occurred, "Tick should not occur (waiting for provider B)"
    print(f"✓ Provider A updated, tick pending (waiting for B)")
    
    # Provider B updates
    request2 = pb.SignalUpdates(
        session_id=session2,
        signals=[pb.SignalUpdate(path="ambient.temp", value=25.0, unit="degC")]
    )
    response2 = stub.UpdateSignals(request2)
    
    # NOW the tick should occur
    assert response2.tick_occurred, "Tick should occur after all providers updated"
    print(f"✓ Provider B updated, tick occurred (t={response2.sim_time_sec:.3f}s)")
    
    # Next update from provider A should again block
    response3 = stub.UpdateSignals(request1)
    assert not response3.tick_occurred, "Next tick should wait for B again"
    print(f"✓ Multi-provider coordination verified")


def test_read_signals(stub):
    """Test signal reading."""
    print(f"\n[TEST] Read Signals")
    
    request = pb.SignalRequest(
        paths=["chamber.temp", "sensor.reading", "display.temp"]
    )
    
    response = stub.ReadSignals(request)
    
    print(f"✓ Read {len(response.signals)} signals:")
    for sig in response.signals:
        print(f"  - {sig.path} = {sig.value:.3f} {sig.unit} (physics_driven={sig.physics_driven})")


def test_reset(stub):
    """Test simulation reset."""
    print(f"\n[TEST] Reset")
    
    request = pb.ResetRequest()
    response = stub.Reset(request)
    
    assert response.success, f"Reset failed: {response.error_message}"
    print("✓ Reset successful")


def main():
    # Determine config path
    repo_root = Path(__file__).parent.parent
    config_path = repo_root / "examples" / "04_yaml_graph" / "graph.yaml"
    
    if not config_path.exists():
        print(f"ERROR: Config file not found: {config_path}")
        sys.exit(1)
    
    # Connect to server
    server_address = "localhost:50051"
    print(f"Connecting to FluxGraph server at {server_address}...")
    
    channel = grpc.insecure_channel(server_address)
    stub = pb_grpc.FluxGraphStub(channel)
    
    try:
        # Wait for server to be ready
        grpc.channel_ready_future(channel).result(timeout=5)
        print("✓ Connected to server\n")
        
        # Run tests
        test_health_check(stub)
        test_load_config(stub, config_path)
        
        # Test single provider
        session = test_register_provider(stub, "test-provider", ["heater", "sensor"])
        test_single_provider_tick(stub, session)
        test_read_signals(stub)
        
        # Reset and test multi-provider
        test_reset(stub)
        # After reset, config is still loaded (same hash), so use different hash to force reload
        test_load_config_with_hash(stub, config_path, "test_config_v2")
        test_multi_provider_coordination(stub)
        
        print("\n" + "="*60)
        print("ALL TESTS PASSED ✓")
        print("="*60)
        
    except grpc.RpcError as e:
        print(f"\nERROR: gRPC call failed")
        print(f"  Code: {e.code()}")
        print(f"  Details: {e.details()}")
        sys.exit(1)
    except Exception as e:
        print(f"\nERROR: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        channel.close()


if __name__ == "__main__":
    main()
