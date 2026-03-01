import os
import subprocess
import sys
import time
import socket
from pathlib import Path
from typing import Any, Iterator, List, Tuple

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
        print(f"WARNING: FLUXGRAPH_SERVER_EXE={env_path} does not exist. Falling back to search.")

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

    required_files = ("fluxgraph_pb2.py", "fluxgraph_pb2_grpc.py")

    def has_required_bindings(path: Path) -> bool:
        return all((path / filename).is_file() for filename in required_files)

    has_bindings = has_required_bindings(python_proto_dir)

    if not has_bindings:
        # Try to generate them using the scripts
        script_ext = ".ps1" if sys.platform == "win32" else ".sh"
        script = root / "scripts" / f"generate_proto_python{script_ext}"

        if script.exists():
            print(f"Generating proto bindings using {script}...")
            cmd = (
                [
                    "powershell",
                    "-ExecutionPolicy",
                    "Bypass",
                    "-File",
                    str(script),
                    "-OutputDir",
                    str(python_proto_dir),
                ]
                if sys.platform == "win32"
                else ["bash", str(script), str(python_proto_dir)]
            )
            try:
                result = subprocess.run(cmd, check=True, capture_output=True, text=True)
                if result.stdout:
                    print(result.stdout)
                if result.stderr:
                    print(result.stderr)
            except subprocess.CalledProcessError as e:
                stdout = e.stdout if isinstance(e.stdout, str) else ""
                stderr = e.stderr if isinstance(e.stderr, str) else ""
                pytest.fail(
                    "Failed to generate protobuf bindings.\n"
                    f"Command: {' '.join(cmd)}\n"
                    f"stdout:\n{stdout}\n"
                    f"stderr:\n{stderr}"
                )

        has_bindings = has_required_bindings(python_proto_dir)

    if not has_bindings:
        missing = [filename for filename in required_files if not (python_proto_dir / filename).is_file()]
        pytest.fail(
            "Python protobuf bindings missing after generation.\n"
            f"Directory: {python_proto_dir}\n"
            f"Missing files: {', '.join(missing)}"
        )

    # Add to sys.path so imports work
    proto_path = str(python_proto_dir)
    if proto_path not in sys.path:
        sys.path.insert(0, proto_path)
    return python_proto_dir


def _allocate_free_port() -> int:
    """Allocate a free ephemeral localhost TCP port."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def _collect_process_output(process: subprocess.Popen[str]) -> Tuple[str, str]:
    """Terminate process if needed and return captured stdout/stderr."""
    if process.poll() is None:
        process.terminate()
        try:
            stdout, stderr = process.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            stdout, stderr = process.communicate(timeout=2)
    else:
        stdout, stderr = process.communicate()
    return stdout or "", stderr or ""


# -----------------------------------------------------------------------------
# Fixtures
# -----------------------------------------------------------------------------


@pytest.fixture(scope="session")
def proto_bindings() -> Path:
    """Ensure protobuf bindings are available for the session."""
    return _ensure_proto_bindings(_repo_root())


@pytest.fixture(scope="session")
def server_exe() -> Path:
    """Resolve server executable path."""
    return _find_server_executable(_repo_root())


@pytest.fixture
def free_port() -> int:
    """Get a free ephemeral port."""
    return _allocate_free_port()


class ServerProcess:
    """Manages a running fluxgraph-server process."""

    def __init__(self, process: subprocess.Popen[str], port: int) -> None:
        self.process = process
        self.port = port
        self.address = f"127.0.0.1:{port}"
        self._stdout_lines: List[str] = []
        self._stderr_lines: List[str] = []

    def stop(self) -> None:
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
def fluxgraph_server(server_exe: Path, free_port: int, proto_bindings: Path) -> Iterator[ServerProcess]:
    """
    Start a fluxgraph-server instance on a random port.
    Yields a ServerProcess object.
    Autostops after test.
    """
    # Import pb modules dynamically after ensuring bindings exist
    import fluxgraph_pb2_grpc as pb_grpc
    import fluxgraph_pb2 as pb

    max_start_attempts = 3
    startup_timeout_sec = 10.0
    startup_failure = "unknown startup failure"

    for attempt in range(1, max_start_attempts + 1):
        port = free_port if attempt == 1 else _allocate_free_port()
        cmd = [str(server_exe), "--port", str(port)]

        # Start process
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            cwd=str(_repo_root()),  # Run from repo root so relative config paths work if needed
        )
        server = ServerProcess(proc, port)

        # Wait for readiness (using gRPC health check)
        channel = grpc.insecure_channel(server.address)
        stub = pb_grpc.FluxGraphStub(channel)

        start_time = time.time()
        ready = False
        last_err: grpc.RpcError | None = None

        while time.time() - start_time < startup_timeout_sec:
            if proc.poll() is not None:
                stdout, stderr = _collect_process_output(proc)
                startup_failure = (
                    f"Server exited during startup on attempt {attempt}/{max_start_attempts} "
                    f"(port={port}, code={proc.returncode}).\n"
                    f"stdout:\n{stdout}\n"
                    f"stderr:\n{stderr}"
                )
                break

            try:
                req = pb.HealthCheckRequest(service="fluxgraph")
                resp = stub.Check(req, timeout=0.5)
                if resp.status == pb.HealthCheckResponse.SERVING:
                    ready = True
                    break
            except grpc.RpcError as e:
                last_err = e
                time.sleep(0.1)

        channel.close()

        if ready:
            yield server
            server.stop()
            return

        if proc.poll() is None:
            stdout, stderr = _collect_process_output(proc)
            startup_failure = (
                f"Server at {server.address} failed readiness on attempt {attempt}/{max_start_attempts} "
                f"within {startup_timeout_sec:.1f}s. Last RPC error: {last_err}\n"
                f"stdout:\n{stdout}\n"
                f"stderr:\n{stderr}"
            )

        if attempt < max_start_attempts:
            print(f"WARNING: {startup_failure}\nRetrying startup...")

    pytest.fail(startup_failure)


@pytest.fixture
def grpc_channel(fluxgraph_server: ServerProcess) -> Iterator[grpc.Channel]:
    """Provide a ready-to-use gRPC channel to the running server."""
    channel = grpc.insecure_channel(fluxgraph_server.address)
    yield channel
    channel.close()


@pytest.fixture
def grpc_stub(grpc_channel: grpc.Channel, proto_bindings: Path) -> Any:
    """Provide a FluxGraph stub."""
    import fluxgraph_pb2_grpc as pb_grpc

    return pb_grpc.FluxGraphStub(grpc_channel)
