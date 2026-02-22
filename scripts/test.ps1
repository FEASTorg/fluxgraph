# FluxGraph test wrapper (preset-first)
#
# Usage:
#   .\scripts\test.ps1 [-Preset <name>] [-Verbose]
#
# Default preset:
#   - Windows: dev-windows-release
#   - Linux/macOS: dev-release

param(
    [string]$Preset = "",
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

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
    $CTestArgs = @("--preset", $Preset)
    if ($Verbose) {
        $CTestArgs += "--verbose"
    }

    Write-Host "[TEST] ctest --preset $Preset" -ForegroundColor Cyan
    & ctest @CTestArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Tests failed"
    }
}
finally {
    Pop-Location
}
