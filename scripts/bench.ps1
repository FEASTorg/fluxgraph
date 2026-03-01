# FluxGraph benchmark wrapper (preset-first)
# Usage:
#   .\scripts\bench.ps1 [-Preset <name>] [-Config <cfg>] [-OutputDir <path>] [-IncludeOptional] [-NoBuild] [-FailOnStatus]

param(
    [string]$Preset = "",
    [string]$Config = "",
    [string]$OutputDir = "",
    [switch]$IncludeOptional,
    [switch]$NoBuild,
    [switch]$FailOnStatus,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$PassThroughArgs
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot

if (-not $Preset) {
    if ($env:OS -eq "Windows_NT") {
        $Preset = "dev-windows-release"
    }
    else {
        $Preset = "dev-release"
    }
}

if (-not $env:VCPKG_ROOT) {
    Write-Warning "VCPKG_ROOT is not set. Presets may fail to configure."
}

$Args = @("$PSScriptRoot/run_benchmarks.py", "--preset", $Preset)

if ($Config) {
    $Args += @("--config", $Config)
}
if ($OutputDir) {
    $Args += @("--output-dir", $OutputDir)
}
if ($IncludeOptional) {
    $Args += "--include-optional"
}
if ($NoBuild) {
    $Args += "--no-build"
}
if ($FailOnStatus) {
    $Args += "--fail-on-status"
}
if ($PassThroughArgs) {
    $Args += $PassThroughArgs
}

Push-Location $RepoRoot
try {
    Write-Host "[BENCH] python $($Args -join ' ')" -ForegroundColor Cyan
    & python @Args
    if ($LASTEXITCODE -ne 0) {
        throw "Benchmark run failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
