"""Integration tests for the FluxGraph gRPC server."""

import grpc
import pytest
import subprocess
import time
from typing import Any, cast


def _pb() -> Any:
    import fluxgraph_pb2 as pb

    return pb


def _valid_yaml_config() -> str:
    return """
models:
  - id: chamber
    type: thermal_mass
    params:
      temp_signal: chamber.temp
      power_signal: chamber.power
      ambient_signal: ambient.temp
      thermal_mass: 1000.0
      heat_transfer_coeff: 10.0
      initial_temp: 25.0

edges:
  - source: heater.output
    target: chamber.power
    transform:
      type: saturation
      params:
        min: 0.0
        max: 1000.0
"""


def _load_config(grpc_stub: Any, pb: Any, config_hash: str = "test_config_v1") -> Any:
    response = grpc_stub.LoadConfig(
        pb.ConfigRequest(
            config_content=_valid_yaml_config(),
            format="yaml",
            config_hash=config_hash,
        )
    )
    assert response.success
    return response


def _register_provider(grpc_stub: Any, pb: Any, provider_id: str = "sim_test") -> str:
    response = grpc_stub.RegisterProvider(
        pb.ProviderRegistration(
            provider_id=provider_id,
            device_ids=["heater0"],
        )
    )
    assert response.success
    assert response.session_id
    return cast(str, response.session_id)


@pytest.fixture
def grpc_stub_dt_025(server_exe: Any, free_port: int, proto_bindings: Any) -> Any:
    """Start a dedicated server instance with --dt=0.25 and return a stub."""
    import fluxgraph_pb2 as pb
    import fluxgraph_pb2_grpc as pb_grpc

    address = f"127.0.0.1:{free_port}"
    proc = subprocess.Popen(
        [str(server_exe), "--port", str(free_port), "--dt", "0.25"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    channel = grpc.insecure_channel(address)
    stub = pb_grpc.FluxGraphStub(channel)

    ready = False
    start = time.time()
    while time.time() - start < 10.0:
        if proc.poll() is not None:
            out, err = proc.communicate(timeout=2)
            pytest.fail(f"Server exited before readiness check.\nstdout:\n{out}\nstderr:\n{err}")

        try:
            resp = stub.Check(pb.HealthCheckRequest(service="fluxgraph"), timeout=0.5)
            if resp.status == pb.HealthCheckResponse.SERVING:
                ready = True
                break
        except grpc.RpcError:
            time.sleep(0.1)

    if not ready:
        proc.terminate()
        out, err = proc.communicate(timeout=2)
        pytest.fail(f"Server failed readiness with --dt=0.25.\nstdout:\n{out}\nstderr:\n{err}")

    try:
        yield stub
    finally:
        channel.close()
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()


@pytest.mark.integration
@pytest.mark.slow
def test_server_health_check(grpc_stub: Any) -> None:
    """Verify server reports healthy status."""
    pb = _pb()
    response = grpc_stub.Check(pb.HealthCheckRequest(service="fluxgraph"))
    assert response.status == pb.HealthCheckResponse.SERVING


@pytest.mark.integration
def test_load_yaml_config(grpc_stub: Any) -> None:
    """Verify valid config loads and hash-based no-op works."""
    pb = _pb()
    first = _load_config(grpc_stub, pb, config_hash="cfg_hash_1")
    assert first.config_changed

    second = _load_config(grpc_stub, pb, config_hash="cfg_hash_1")
    assert not second.config_changed


@pytest.mark.integration
def test_provider_registration(grpc_stub: Any) -> None:
    """Verify provider registration succeeds and rejects duplicate provider_id."""
    pb = _pb()
    _load_config(grpc_stub, pb)

    _register_provider(grpc_stub, pb, provider_id="provider_a")

    with pytest.raises(grpc.RpcError) as exc_info:
        grpc_stub.RegisterProvider(
            pb.ProviderRegistration(
                provider_id="provider_a",
                device_ids=["heater1"],
            )
        )
    assert exc_info.value.code() == grpc.StatusCode.ALREADY_EXISTS


@pytest.mark.integration
def test_signal_lifecycle(grpc_stub: Any) -> None:
    """Load config, register provider, update input signal, and read it back."""
    pb = _pb()
    _load_config(grpc_stub, pb)
    session_id = _register_provider(grpc_stub, pb, provider_id="provider_signal")

    tick = grpc_stub.UpdateSignals(
        pb.SignalUpdates(
            session_id=session_id,
            signals=[
                pb.SignalUpdate(
                    path="heater.output",
                    value=500.0,
                    unit="W",
                )
            ],
        )
    )
    assert tick.tick_occurred
    assert tick.sim_time_sec > 0.0

    read = grpc_stub.ReadSignals(pb.SignalRequest(paths=["heater.output"]))
    assert len(read.signals) == 1
    assert read.signals[0].path == "heater.output"
    assert read.signals[0].value == pytest.approx(500.0)
    assert read.signals[0].unit == "W"


@pytest.mark.integration
def test_invalid_config_handling(grpc_stub: Any) -> None:
    """Verify malformed YAML is rejected with INVALID_ARGUMENT."""
    pb = _pb()
    bad_config = "models: ["

    with pytest.raises(grpc.RpcError) as exc_info:
        grpc_stub.LoadConfig(pb.ConfigRequest(config_content=bad_config, format="yaml"))

    assert exc_info.value.code() == grpc.StatusCode.INVALID_ARGUMENT


@pytest.mark.integration
def test_reset_functionality(grpc_stub: Any) -> None:
    """Verify reset succeeds and clears written signal state."""
    pb = _pb()
    _load_config(grpc_stub, pb)
    session_id = _register_provider(grpc_stub, pb, provider_id="provider_reset")

    grpc_stub.UpdateSignals(
        pb.SignalUpdates(
            session_id=session_id,
            signals=[pb.SignalUpdate(path="heater.output", value=321.0, unit="W")],
        )
    )

    before_reset = grpc_stub.ReadSignals(pb.SignalRequest(paths=["heater.output"]))
    assert len(before_reset.signals) == 1
    assert before_reset.signals[0].value == pytest.approx(321.0)

    reset = grpc_stub.Reset(pb.ResetRequest())
    assert reset.success

    after_reset = grpc_stub.ReadSignals(pb.SignalRequest(paths=["heater.output"]))
    assert len(after_reset.signals) == 1
    assert after_reset.signals[0].value == pytest.approx(0.0)


@pytest.mark.integration
def test_reject_protected_signal_writes(grpc_stub: Any) -> None:
    """Providers cannot write to model-owned or derived target signals."""
    pb = _pb()
    _load_config(grpc_stub, pb)
    session_id = _register_provider(grpc_stub, pb, provider_id="provider_protected")

    with pytest.raises(grpc.RpcError) as physics_exc:
        grpc_stub.UpdateSignals(
            pb.SignalUpdates(
                session_id=session_id,
                signals=[pb.SignalUpdate(path="chamber.temp", value=99.0, unit="degC")],
            )
        )
    assert physics_exc.value.code() == grpc.StatusCode.PERMISSION_DENIED

    with pytest.raises(grpc.RpcError) as derived_exc:
        grpc_stub.UpdateSignals(
            pb.SignalUpdates(
                session_id=session_id,
                signals=[pb.SignalUpdate(path="chamber.power", value=99.0, unit="W")],
            )
        )
    assert derived_exc.value.code() == grpc.StatusCode.PERMISSION_DENIED


@pytest.mark.integration
def test_dt_flag_controls_runtime_timestep(grpc_stub_dt_025: Any) -> None:
    """Server --dt must control simulated time progression."""
    pb = _pb()
    _load_config(grpc_stub_dt_025, pb, config_hash="cfg_dt_025")
    session_id = _register_provider(grpc_stub_dt_025, pb, provider_id="provider_dt")

    tick1 = grpc_stub_dt_025.UpdateSignals(
        pb.SignalUpdates(
            session_id=session_id,
            signals=[pb.SignalUpdate(path="heater.output", value=100.0, unit="W")],
        )
    )
    assert tick1.tick_occurred
    assert tick1.sim_time_sec == pytest.approx(0.25)

    tick2 = grpc_stub_dt_025.UpdateSignals(
        pb.SignalUpdates(
            session_id=session_id,
            signals=[pb.SignalUpdate(path="heater.output", value=100.0, unit="W")],
        )
    )
    assert tick2.tick_occurred
    assert tick2.sim_time_sec == pytest.approx(0.50)


@pytest.mark.integration
def test_stability_validation_rejects_unsafe_dt(grpc_stub: Any) -> None:
    """LoadConfig should fail when model stability limit is below runtime dt."""
    pb = _pb()
    unstable_yaml = """
models:
  - id: unstable
    type: thermal_mass
    params:
      temp_signal: unstable.temp
      power_signal: unstable.power
      ambient_signal: unstable.ambient
      thermal_mass: 1.0
      heat_transfer_coeff: 100.0
      initial_temp: 25.0
"""

    with pytest.raises(grpc.RpcError) as exc_info:
        grpc_stub.LoadConfig(
            pb.ConfigRequest(
                config_content=unstable_yaml,
                format="yaml",
                config_hash="unstable_cfg",
            )
        )

    assert exc_info.value.code() == grpc.StatusCode.INVALID_ARGUMENT
    assert "Stability violation" in exc_info.value.details()
