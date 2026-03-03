# Numerical Methods Policy

## Scope

This document defines the numerical integration policy for FluxGraph models.
It complements `docs/semantics_spec.md` and is authoritative for solver
selection and stability interpretation.
Validation protocol details are documented in `docs/validation-methodology.md`.

## Current Policy

1. Integration method selection is explicit per model via model parameters.
2. If not specified, the deterministic default is `forward_euler`.
3. Methods are selected at compile time and remain fixed at runtime.
4. Runtime behavior must be deterministic for identical inputs and `dt`.

## ThermalMassModel Integration Methods

`thermal_mass` supports:

1. `forward_euler` (default)
2. `rk4` (classic fourth-order Runge-Kutta)

Selection is provided via:

```cpp
model.params["integration_method"] = std::string("forward_euler");
// or
model.params["integration_method"] = std::string("rk4");
```

Unknown method names are compile-time errors.

## Stability Limits

For the linear thermal model `dT/dt = (P - h*(T - T_amb)) / C`, with
`k = h/C`:

1. `forward_euler`: `dt < 2/k = 2*C/h`
2. `rk4`: `dt < 2.785293563/k = 2.785293563*C/h`

If `h <= 0`, the model is treated as unconditionally stable for this criterion.

## Validation Expectations

Validation runs must report:

1. Error metrics (`L2`, `Linf`) versus analytical references.
2. Convergence behavior as `dt` is refined.
3. Determinism checks for each supported integration method.

## Forward Compatibility

Future methods (for example trapezoidal or implicit schemes) must define:

1. Stability policy and limits.
2. Deterministic selection and defaults.
3. Regression and analytical validation coverage before release.

## Reproducible Validation Run

Use the validation runner to produce validation artifacts:

```bash
python scripts/run_numerical_validation.py --preset dev-release --enforce-order
```

Windows example:

```powershell
python .\scripts\run_numerical_validation.py --preset dev-windows-release --config Release --enforce-order
```

CI evidence runs are produced by:

1. `.github/workflows/numerical-validation-evidence.yml`
