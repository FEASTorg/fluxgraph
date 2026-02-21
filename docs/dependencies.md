# Dependency, Build, and CI Governance

This document defines FluxGraph policy for dependency management, preset usage, CI lanes, and versioning.

## vcpkg Policy

1. `vcpkg-configuration.json` is the canonical baseline source.
2. `builtin-baseline` is not used.
3. Lockfile pinning is deferred for now; baseline pinning stays in `vcpkg-configuration.json`.

## Dependency Model Policy

- Governed dependencies are resolved via vcpkg + `find_package(...)`.
- Optional loader behavior is controlled by `FLUXGRAPH_JSON_ENABLED` and `FLUXGRAPH_YAML_ENABLED`.
- Dependency transport changes must not regress OFF/ON loader build combinations.

## Versioning Policy

- FluxGraph follows independent SemVer (`MAJOR.MINOR.PATCH`).
- Public API, schema, or build-surface changes require version-bump decision and changelog note.

## CI Lane Tiers

- **Required**: core build/test lane.
- **Advisory/matrix**: JSON, YAML, server-enabled lanes.
- **Optional heavy lanes**: extended sanitizer/stress/integration runs.

## Preset Baseline and Exception Policy

Baseline names:

- `dev-debug`, `dev-release`, `ci-linux-release`, `ci-windows-release`
- specialized as supported: `ci-asan`, `ci-ubsan`, `ci-tsan`, `ci-coverage`

Rules:

1. CI should call presets directly.
2. CI-only deviations must be explicit and documented.
3. Repo-specific extension presets are allowed for feature matrices.

## Dual-Run Policy

During migration of legacy paths:

- run legacy and new paths in parallel,
- minimum 5 consecutive green runs,
- preferred 10 runs before removing legacy paths.
