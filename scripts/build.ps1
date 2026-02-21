# FluxGraph build wrapper (preset-first)
#
# Usage:
#   .\scripts\build.ps1 [-Preset <name>] [-CleanFirst] [-Jobs <N>]

param(
    [string]$Preset = "dev-release",
    [switch]$CleanFirst,
    [int]$Jobs = 0
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

if (-not $env:VCPKG_ROOT) {
    Write-Error "VCPKG_ROOT is not set. Presets expect vcpkg toolchain at `$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
}

Push-Location $RepoRoot
try {
    Write-Host "[CONFIGURE] cmake --preset $Preset" -ForegroundColor Cyan
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "Configure failed"
    }

    $BuildArgs = @("--preset", $Preset)
    if ($CleanFirst) {
        $BuildArgs += "--clean-first"
    }
    if ($Jobs -gt 0) {
        $BuildArgs += "--parallel"
        $BuildArgs += "$Jobs"
    }

    Write-Host "[BUILD] cmake --build --preset $Preset" -ForegroundColor Cyan
    & cmake --build @BuildArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed"
    }
}
finally {
    Pop-Location
}
