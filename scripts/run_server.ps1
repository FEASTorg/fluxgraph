# FluxGraph Server Runner (Windows)
#
# Usage:
#   .\scripts\run_server.ps1 [-Config <Release|Debug|RelWithDebInfo>] [-Port <port>] [-ConfigFile <path>] [-TimeStep <seconds>]

param(
    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string]$Config = "Release",
    [int]$Port = 50051,
    [string]$ConfigFile = "",
    [double]$TimeStep = 0.1
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

Write-Host "============================================" -ForegroundColor Cyan
Write-Host "FluxGraph gRPC Server" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan

# Find server executable (check multiple possible build directories)
$ServerExe = $null
$PossiblePaths = @(
    "build-$($Config.ToLower())-server\server\$Config\fluxgraph-server.exe",
    "build-server\server\$Config\fluxgraph-server.exe",
    "build\server\$Config\fluxgraph-server.exe"
)

foreach ($path in $PossiblePaths) {
    $fullPath = Join-Path $RepoRoot $path
    if (Test-Path $fullPath) {
        $ServerExe = $fullPath
        break
    }
}

if (-not $ServerExe) {
    Write-Host "[ERROR] Server executable not found. Tried:" -ForegroundColor Red
    foreach ($path in $PossiblePaths) {
        Write-Host "  - $path" -ForegroundColor Gray
    }
    Write-Host "`nBuild the server first: .\scripts\build.ps1 -Server -Config $Config" -ForegroundColor Yellow
    exit 1
}

# Build arguments
$Args = @("--port", $Port, "--dt", $TimeStep)

if ($ConfigFile) {
    if (-not (Test-Path $ConfigFile)) {
        Write-Host "[ERROR] Config file not found: $ConfigFile" -ForegroundColor Red
        exit 1
    }
    $Args += "--config"
    $Args += $ConfigFile
}

Write-Host "Port:        $Port" -ForegroundColor White
Write-Host "Timestep:    $TimeStep sec" -ForegroundColor White
if ($ConfigFile) {
    Write-Host "Config:      $ConfigFile" -ForegroundColor White
}
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

# Run server
& $ServerExe @Args
