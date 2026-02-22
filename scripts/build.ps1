# FluxGraph build wrapper (preset-first)
#
# Usage:
#   .\scripts\build.ps1 [-Preset <name>] [-CleanFirst] [-Jobs <N>]
#
# Default preset:
#   - Windows: dev-windows-release
#   - Linux/macOS: dev-release

param(
    [string]$Preset = "",
    [switch]$CleanFirst,
    [int]$Jobs = 0
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

if (-not $env:VCPKG_ROOT) {
    Write-Error "VCPKG_ROOT is not set. Presets expect vcpkg toolchain at `$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
}

if (-not $Preset) {
    if ($env:OS -eq "Windows_NT") {
        $Preset = "dev-windows-release"
    }
    else {
        $Preset = "dev-release"
    }
}
if (($env:OS -eq "Windows_NT") -and $Preset -in @("dev-release", "dev-debug")) {
    throw "Preset '$Preset' uses Ninja and may select MinGW on Windows. Use 'dev-windows-release', 'dev-windows-debug', or 'ci-windows-release'."
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
