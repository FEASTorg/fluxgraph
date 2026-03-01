# Performance Benchmarks

## Purpose

This document defines the reproducible benchmark workflow for FluxGraph.

Goals:

1. Provide repeatable measurement artifacts for Phase 2 gates.
2. Separate measured evidence from narrative claims.
3. Keep benchmark runs deterministic enough for regression comparison.

## Scope

Current benchmark executables:

1. `benchmark_signal_store`
2. `benchmark_namespace`
3. `benchmark_tick`
4. `json_loader_bench` (optional, when `FLUXGRAPH_JSON_ENABLED=ON`)
5. `yaml_loader_bench` (optional, when `FLUXGRAPH_YAML_ENABLED=ON`)

## Reproducible Runner

Use the benchmark wrapper scripts.

Linux/macOS:

```bash
bash ./scripts/bench.sh --preset dev-release
```

Windows PowerShell:

```powershell
.\scripts\bench.ps1 -Preset dev-windows-release -Config Release
```

Optional loader benchmarks:

```bash
bash ./scripts/bench.sh --preset dev-release --include-optional
```

Strict status enforcement (for gated runs):

```bash
bash ./scripts/bench.sh --preset dev-release --fail-on-status
```

The wrappers call `scripts/run_benchmarks.py`, which:

1. Configures/builds benchmark targets (unless `--no-build` is set).
2. Runs each benchmark executable.
3. Captures stdout/stderr logs per target.
4. Emits a machine-readable manifest with environment and git metadata.

By default, status failures are reported but do not fail the run; execution failures still fail.
Use `--fail-on-status` when running gate-enforced benchmark checks.

## Artifact Contract

Artifacts are stored under:

`artifacts/benchmarks/<timestamp>_<preset>/`

Required files:

1. `benchmark_results.json`
2. `<target>.stdout.log`
3. `<target>.stderr.log`
4. `configure.stdout.log`, `configure.stderr.log` (when build enabled)
5. `build.stdout.log`, `build.stderr.log` (when build enabled)

`benchmark_results.json` contains:

1. Timestamp (UTC)
2. Preset/config/build directory
3. Platform, hostname, Python version
4. Git commit hash and dirty-worktree flag
5. Per-benchmark executable path, command, exit code, duration, parsed PASS/FAIL status lines

## Evidence Rules

For any published performance claim, attach:

1. Commit hash used for the run.
2. Exact benchmark command.
3. Full artifact directory or archived equivalent.
4. Hardware and OS details (captured in manifest + release notes).
5. Comparison baseline (previous artifact manifest).

Claims without linked artifacts are treated as unsupported.

## CI Guidance

Benchmarks are intentionally not part of default CI pass/fail lanes due to runtime and host variance.

Recommended:

1. Run benchmark workflow on demand (`workflow_dispatch`) or scheduled lane.
2. Store artifacts as CI build artifacts.
3. Compare against previous baseline with explicit tolerance policy.

## Next Phase 2 Steps

1. Add allocation-count instrumentation in tick path.
2. Define numeric regression thresholds per workload class.
3. Introduce automated baseline comparison tooling.
