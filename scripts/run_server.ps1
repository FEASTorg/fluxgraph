# FluxGraph Server Runner (Windows)
#
# Usage:
#   .\scripts\run_server.ps1 [-Preset <name>] [-BuildDir <path>] [-Port <port>] [-ConfigFile <path>] [-TimeStep <seconds>]

param(
    [string]$Preset = "ci-linux-release-server",
    [string]$BuildDir = "",
    [int]$Port = 50051,
    [string]$ConfigFile = "",
    [double]$TimeStep = 0.1
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

if (-not $BuildDir) {
    if ($Preset -eq "ci-linux-release-server") {
        $BuildDir = Join-Path $RepoRoot "build-server"
    }
    else {
        $BuildDir = Join-Path $RepoRoot "build\$Preset"
    }
}

Write-Host "============================================" -ForegroundColor Cyan
Write-Host "FluxGraph gRPC Server" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan

$ServerExe = $null
$PossiblePaths = @(
    (Join-Path $BuildDir "server\fluxgraph-server.exe"),
    (Join-Path $BuildDir "server\Release\fluxgraph-server.exe"),
    (Join-Path $BuildDir "server\Debug\fluxgraph-server.exe")
)

foreach ($path in $PossiblePaths) {
    if (Test-Path $path) {
        $ServerExe = $path
        break
    }
}

if (-not $ServerExe) {
    Write-Host "[ERROR] Server executable not found. Tried:" -ForegroundColor Red
    foreach ($path in $PossiblePaths) {
        Write-Host "  - $path" -ForegroundColor Gray
    }
    Write-Host "`nBuild first: .\scripts\build.ps1 -Preset $Preset" -ForegroundColor Yellow
    exit 1
}

$Args = @("--port", $Port, "--dt", $TimeStep)

if ($ConfigFile) {
    if (-not (Test-Path $ConfigFile)) {
        Write-Host "[ERROR] Config file not found: $ConfigFile" -ForegroundColor Red
        exit 1
    }
    $Args += "--config"
    $Args += $ConfigFile
}

Write-Host "Preset:      $Preset" -ForegroundColor White
Write-Host "Build dir:   $BuildDir" -ForegroundColor White
Write-Host "Port:        $Port" -ForegroundColor White
Write-Host "Timestep:    $TimeStep sec" -ForegroundColor White
if ($ConfigFile) {
    Write-Host "Config:      $ConfigFile" -ForegroundColor White
}
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

& $ServerExe @Args
