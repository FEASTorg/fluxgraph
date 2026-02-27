import os
import subprocess
import sys
import time
import socket
from pathlib import Path
from typing import List

import grpc
import pytest

# Types for type hinting

# -----------------------------------------------------------------------------
# Path Resolution & Environment
# -----------------------------------------------------------------------------


def _repo_root() -> Path:
    """Return the repository root directory."""
    return Path(__file__).resolve().parent.parent


def _find_server_executable(root: Path) -> Path:
    """Resolve the fluxgraph-server executable path."""
    # Check environment variable first (set by CMake/CI)
    env_path = os.environ.get("FLUXGRAPH_SERVER_EXE")
    if env_path:
        path = Path(env_path)
        if path.exists() and path.is_file():
            return path.resolve()
        # Warn but fall back if env var points to nowhere
        print(
            f"WARNING: FLUXGRAPH_SERVER_EXE={env_path} does not exist. Falling back to search."
        )

    # Search common build locations
    names = ["fluxgraph-server.exe", "fluxgraph-server"]
    build_dirs = [
        "build/server",
        "build/server/Release",
        "build/server/Debug",
        "build-server",
        "build-server/Release",
        "build-server/Debug",
    ]

    for build_dir in build_dirs:
        for name in names:
            candidate = root / build_dir / name
            if candidate.exists() and candidate.is_file():
                return candidate.resolve()

    pytest.skip("Could not find fluxgraph-server executable. Build the server first.")
    return Path("NOT_FOUND")


def _ensure_proto_bindings(root: Path) -> Path:
    """Ensure Python protobuf bindings exist and are in sys.path."""

    # Check environment variable (set by CMake/CI)
    env_path = os.environ.get("FLUXGRAPH_PROTO_PYTHON_DIR")
    if env_path:
        python_proto_dir = Path(env_path)
    else:
        # Default location
        python_proto_dir = root / "build-server" / "python"

    # Minimal check: does the directory exist and contain _pb2.py files?
    has_bindings = False
    if python_proto_dir.exists():
        if list(python_proto_dir.glob("*_pb2.py")):
            has_bindings = True

    if not has_bindings:
        # Try to generate them using the scripts
        script_ext = ".ps1" if sys.platform == "win32" else ".sh"
        script = root / "scripts" / f"generate_proto_python{script_ext}"

        if script.exists():
            print(f"Generating proto bindings using {script}...")
            cmd = (
                ["powershell", "-ExecutionPolicy", "Bypass", "-File", str(script)]
                if sys.platform == "win32"
                else ["bash", str(script)]
            )
            try:
                subprocess.run(cmd, check=True, capture_output=True)
                has_bindings = True
            except subprocess.CalledProcessError as e:
                print(f"Failed to generate bindings: {e.stderr.decode()}")

    if not has_bindings:
        pytest.fail(
            f"Python protobuf bindings not found at {python_proto_dir}. Run scripts/generate_proto_python.{('ps1' if sys.platform == 'win32' else 'sh')}"
        )

    # Add to sys.path so imports work
    sys.path.insert(0, str(python_proto_dir))
    return python_proto_dir


# -----------------------------------------------------------------------------
# Fixtures
# -----------------------------------------------------------------------------


@pytest.fixture(scope="session")
def proto_bindings():
    """Ensure protobuf bindings are available for the session."""
    return _ensure_proto_bindings(_repo_root())


@pytest.fixture(scope="session")
def server_exe():
    """Resolve server executable path."""
    return _find_server_executable(_repo_root())


@pytest.fixture
def free_port():
    """Get a free ephemeral port."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class ServerProcess:
    """Manages a running fluxgraph-server process."""

    def __init__(self, process: subprocess.Popen, port: int):
        self.process = process
        self.port = port
        self.address = f"127.0.0.1:{port}"
        self._stdout_lines: List[str] = []
        self._stderr_lines: List[str] = []

    def stop(self):
        """Terminate the server process."""
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()

    def get_logs(self) -> str:
        """Return combined stdout/stderr logs."""
        # Note: In a real implementation with continuous output,
        # we'd use a background thread to capture output to avoid blocking buffers.
        # fast implementation for now assuming reasonable log volume for tests.
        return "Logs capture not fully implemented for non-blocking IO"


@pytest.fixture
def fluxgraph_server(server_exe: Path, free_port: int, proto_bindings):
    """
    Start a fluxgraph-server instance on a random port.
    Yields a ServerProcess object.
    Autostops after test.
    """
    # Import pb modules dynamically after ensuring bindings exist
    import fluxgraph_pb2_grpc as pb_grpc
    import fluxgraph_pb2 as pb

    cmd = [str(server_exe), "--port", str(free_port)]

    # Start process
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        cwd=str(
            _repo_root()
        ),  # Run from repo root so relative config paths work if needed
    )

    server = ServerProcess(proc, free_port)

    # Wait for readiness (using gRPC health check)
    address = f"127.0.0.1:{free_port}"
    channel = grpc.insecure_channel(address)
    stub = pb_grpc.HealthCheckStub(channel)

    start_time = time.time()
    ready = False
    last_err = None

    while time.time() - start_time < 5.0:
        if proc.poll() is not None:
            pytest.fail(
                f"Server process died immediately with code {proc.returncode}. Stderr: {proc.stderr.read()}"
            )

        try:
            req = pb.HealthCheckRequest(service="fluxgraph")
            resp = stub.Check(req)
            if resp.status == pb.HealthCheckResponse.SERVING:
                ready = True
                break
        except grpc.RpcError as e:
            last_err = e
            time.sleep(0.1)

    if not ready:
        proc.kill()
        pytest.fail(
            f"Server at {address} failed to become ready in 5s. Last error: {last_err}"
        )

    yield server

    server.stop()


@pytest.fixture
def grpc_channel(fluxgraph_server):
    """Provide a ready-to-use gRPC channel to the running server."""
    channel = grpc.insecure_channel(fluxgraph_server.address)
    yield channel
    channel.close()


@pytest.fixture
def grpc_stub(grpc_channel, proto_bindings):
    """Provide a FluxGraphService stub."""
    import fluxgraph_pb2_grpc as pb_grpc

    return pb_grpc.FluxGraphServiceStub(grpc_channel)
