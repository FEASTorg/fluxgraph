# FluxGraph Semantics Specification

**Status:** Phase 0 baseline (pending dual sign-off)  
**Version:** 0.1.0-phase0  
**Last Updated:** March 1, 2026

## 1. Scope and Authority

This document is the normative semantic contract for FluxGraph runtime behavior.

1. If this document conflicts with README, architecture notes, examples, or API docs, this document is authoritative.
2. All public claims about execution behavior must be derivable from this document.
3. Any semantic change requires an update to this document before merge.

## 2. Evaluated Execution Models and Selection

## Option A: Snapshot Staged Semantics

Definition: every edge reads from a fixed pre-tick snapshot and writes to next-state storage; no within-tick graph propagation.

Pros:

1. Very simple reasoning model.
2. Strong isolation between read and write phases.

Cons:

1. Hidden one-tick latency across edge chains.
2. Topological ordering has reduced semantic impact.
3. Difficult to align with user expectation for DAG dataflow.

## Option B: Immediate Topological Propagation Semantics

Definition: edges execute in deterministic topological order and read latest visible values; upstream edge writes are visible to downstream edges in the same tick.

Pros:

1. Natural DAG signal-flow semantics.
2. Explicit delays are the only source of temporal delay.
3. Better alignment with scientific communication for causal graphs.

Cons:

1. Requires stricter compiler/runtime contracts (cycle policy, writer policy, deterministic ordering).

## Selected Model (Normative)

FluxGraph adopts **Immediate Topological Propagation Semantics**.

## 3. Time and Tick Model

We define a discrete tick index `k` with tick boundaries `t_k`.

1. `dt_k > 0` is required for every tick.
2. The state visible to application and providers before `tick(k)` is `S_k`.
3. `tick(k)` computes `S_{k+1}` and command set `C_{k+1}`.
4. No concurrent tick execution is permitted.
5. For publication-grade reproducibility, runs are expected to use constant `dt` unless explicitly documented otherwise.

## 4. Normative Tick Ordering

Each `tick(dt, store)` executes in this order:

1. **Input Boundary Freeze**
   - Provider/application writes completed before tick begin are considered part of `S_k`.
2. **Model Update Stage**
   - Models advance internal state by `dt` and write model outputs into store.
3. **Edge Propagation Stage**
   - Edges execute in deterministic topological order.
   - Edge output writes are immediately visible to downstream edges in the same stage.
4. **Rule Evaluation Stage**
   - Rules evaluate against post-model/post-edge state (`S_{k+1}`).
   - Matching rules emit commands for external execution.
5. **Tick Commit**
   - `S_{k+1}` and `C_{k+1}` become externally visible.

Determinism requirements:

1. Topological ordering tie-break must be stable and deterministic.
2. Command ordering must be deterministic for identical inputs and configuration.

## 5. Cycle and Algebraic Loop Policy

1. The directed graph induced by all **non-delay** edges must be acyclic.
2. A cycle is only legal if every feedback loop is broken by at least one explicit delay element.
3. Compile-time cycle diagnostics must identify at least one concrete loop path.
4. Cycles without explicit delay are compile-time errors.

## 6. Delay Semantics

Delay behavior is explicit and never implicit.

1. Delay transform parameter `delay_sec` maps to integer ticks by:
   - `N = max(1, round(delay_sec / dt_ref))`
2. `dt_ref` is the runtime tick period used for model execution in the deployment profile.
3. Delay output at tick `k` is input value from tick `k-N`.
4. Initial delay buffer fill value is `0.0` unless an explicit initializer is introduced in a future spec revision.
5. If runtime `dt` deviates from `dt_ref` beyond tolerance, runtime must reject execution or reinitialize delay buffers according to documented policy.

## 7. Signal Ownership and Write Authority

Each signal has exactly one owning writer class per tick:

1. **Model-owned (physics-owned):** writable by model stage only.
2. **Edge-owned (derived):** writable by edge stage only.
3. **External-owned (provider/application input):** writable by external update path only.

Rules:

1. Multi-writer targets are compile-time errors.
2. External writes to model-owned or edge-owned signals are runtime errors.
3. Engine internal stages must not overwrite signals owned by different stages.

## 8. Unit Semantics

Unit handling policy is strict and deterministic.

1. Every signal has declared unit metadata (`"dimensionless"` allowed).
2. Writes must match declared unit policy exactly unless explicit conversion rules are defined.
3. Load/compile must reject incompatible unit contracts where inferable from graph specification.
4. Runtime unit mismatch is a hard error (not silent coercion).
5. Claims of dimensional analysis are only valid when compile/load/runtime unit contracts are all enforced.

## 9. Timestep and Stability Policy

1. `dt <= min(model_stability_limit)` is required unless a model explicitly documents unconditional stability.
2. Stability validation must run in active compile/load path for target runtime `dt`.
3. Server CLI `--dt` must be wired into runtime tick behavior and stability checks.
4. Violations are hard failures (load rejection), not warnings.

## 10. Non-goals in This Spec Version

1. Parallel edge execution semantics.
2. Adaptive timestep controller semantics.
3. Implicit solver algebraic-loop handling.
4. Automatic unit conversion ontology.

## 11. Conformance Snapshot (March 1, 2026)

This section records current implementation alignment against the normative contract.

1. Engine currently snapshots edge sources and executes edges before models: **non-conformant** with Sections 4 and 6.
2. Rule conditions are currently stubbed false in compiler: **non-conformant** with Section 4 rule stage semantics.
3. Stability validator exists but is not called in compile/load path: **non-conformant** with Section 9.
4. Server `--dt` flag is parsed but not wired into service `dt_`: **non-conformant** with Section 9.
5. External write protection for physics-owned signals is not enforced in server update path: **non-conformant** with Section 7.
6. Unit checks are partial and do not yet provide full compile/load/runtime contract enforcement: **partial** for Section 8.

These gaps define mandatory Phase 1 contract work.
