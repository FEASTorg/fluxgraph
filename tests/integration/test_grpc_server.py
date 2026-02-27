"""
Integration tests for FluxGraph gRPC server using pytest.
Replaces legacy test_grpc_integration.py
"""

import pytest
import grpc

# We need to import the protobuf modules.
# They are guaranteed to be in path by the 'proto_bindings' fixture,
# but mypy doesn't know that, so we use type: ignore or dynamic imports if strictness requires.
# Since conftest handles sys.path, standard imports work at runtime.

try:
    import fluxgraph_pb2 as pb
except ImportError:
    # ensuring linter doesn't freak out before generation
    pass


@pytest.mark.integration
@pytest.mark.slow
def test_server_health_check(grpc_stub):
    """Verify server reports healthy status."""
    request = pb.HealthCheckRequest(service="fluxgraph")
    response = grpc_stub.Check(request)
    assert response.status == pb.HealthCheckResponse.SERVING


@pytest.mark.integration
def test_load_yaml_config(grpc_stub):
    """Verify loading a valid YAML configuration."""
    yaml_config = """
    time:
      step: 0.1
    store:
      signals:
        - id: "temp_c"
          type: "double"
          initial: 25.0
    """
    req = pb.ConfigRequest(
        config_content=yaml_config, format="yaml", config_hash="test_yaml_v1"
    )
    resp = grpc_stub.LoadConfig(req)
    assert resp.success is True
    assert "Loaded" in resp.message
    assert resp.active_hash == "test_yaml_v1"


@pytest.mark.integration
def test_provider_registration(grpc_stub):
    """Verify provider registration workflow."""
    # 1. Register
    reg_req = pb.ProviderRequest(
        provider_id="sim_test", capabilities=["thermal", "motor"]
    )
    reg_resp = grpc_stub.RegisterProvider(reg_req)
    assert reg_resp.success is True

    # 2. Verify with Health Check (often providers register status)
    # Note: FluxGraph simplistic health check might not reflect providers yet,
    # but the call should succeed.


@pytest.mark.integration
def test_signal_lifecycle(grpc_stub):
    """Test full cycle: Load Config -> Update Signal -> Tick -> Read Signal."""

    # 1. Load Config with a signal
    yaml_config = """
    store:
      signals:
        - id: "velocity"
          type: "double"
          initial: 0.0
    """
    grpc_stub.LoadConfig(pb.ConfigRequest(config_content=yaml_config, format="yaml"))

    # 2. Update Signal
    update_req = pb.SignalUpdate(
        updates=[pb.SignalValue(id="velocity", value_double=10.5)]
    )
    update_resp = grpc_stub.UpdateSignals(update_req)
    assert update_resp.success is True

    # 3. Tick (advance time)
    tick_req = pb.TickRequest(delta_seconds=0.1)
    grpc_stub.Tick(tick_req)
    # assert tick_resp.sim_time > 0  (depends on implementation start time)

    # 4. Read Signal
    read_req = pb.SignalReadRequest(ids=["velocity"])
    read_resp = grpc_stub.ReadSignals(read_req)

    assert len(read_resp.values) == 1
    assert read_resp.values[0].id == "velocity"
    assert read_resp.values[0].value_double == pytest.approx(10.5)


@pytest.mark.integration
def test_invalid_config_handling(grpc_stub):
    """Verify backend rejects malformed YAML."""
    bad_config = """
    time:
      step: "not_a_number"  # Invalid type
    """
    req = pb.ConfigRequest(config_content=bad_config, format="yaml")

    # Depending on implementation, this might raise RpcError or return success=False
    # Testing robustly for either:
    try:
        resp = grpc_stub.LoadConfig(req)
        assert resp.success is False or "error" in resp.message.lower()
    except grpc.RpcError as e:
        assert e.code() in (grpc.StatusCode.INVALID_ARGUMENT, grpc.StatusCode.INTERNAL)


@pytest.mark.integration
def test_reset_functionality(grpc_stub):
    """Verify reset clears state."""
    # Load config and set state
    yaml_config = """
    store:
        signals:
            - id: "counter"
              type: "int64"
              initial: 0
    """
    grpc_stub.LoadConfig(pb.ConfigRequest(config_content=yaml_config, format="yaml"))

    # Set to 42
    grpc_stub.UpdateSignals(
        pb.SignalUpdate(updates=[pb.SignalValue(id="counter", value_int=42)])
    )

    # Verify set
    read1 = grpc_stub.ReadSignals(pb.SignalReadRequest(ids=["counter"]))
    assert read1.values[0].value_int == 42

    # Reset
    grpc_stub.Reset(pb.ResetRequest(mode="soft"))

    # Verify reset (should be back to initial 0)
    read2 = grpc_stub.ReadSignals(pb.SignalReadRequest(ids=["counter"]))
    assert read2.values[0].value_int == 0
