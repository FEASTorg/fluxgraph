# FluxGraph test wrapper (preset-first)
#
# Usage:
#   .\scripts\test.ps1 [-Preset <name>] [-Verbose]

param(
    [string]$Preset = "dev-release",
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

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
