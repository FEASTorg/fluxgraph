# Generate Python Protobuf Bindings (Windows)
#
# Usage:
#   .\scripts\generate-proto-python.ps1 [-OutputDir <path>]

param(
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$ProtoFile = Join-Path $RepoRoot "proto\fluxgraph.proto"
$ProtoDir = Join-Path $RepoRoot "proto"

if (-not $OutputDir) {
    $OutputDir = Join-Path $RepoRoot "build-server\python"
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
}

Write-Host "============================================" -ForegroundColor Cyan
Write-Host "Generate Python Protobuf Bindings" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "Proto:       $ProtoFile" -ForegroundColor White
Write-Host "Output:      $OutputDir" -ForegroundColor White
Write-Host "============================================" -ForegroundColor Cyan

# Check if proto file exists
if (-not (Test-Path $ProtoFile)) {
    Write-Host "[ERROR] Proto file not found: $ProtoFile" -ForegroundColor Red
    exit 1
}

# Find Python executable (prefer venv)
$PythonExe = $null
$VenvPath = Join-Path $RepoRoot ".venv-fxg"

if (Test-Path (Join-Path $VenvPath "Scripts\python.exe")) {
    $PythonExe = Join-Path $VenvPath "Scripts\python.exe"
    Write-Host "Using venv Python: $PythonExe" -ForegroundColor Gray
} else {
    $PythonExe = Get-Command python -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
    if (-not $PythonExe) {
        Write-Host "[ERROR] Python not found. Install Python 3.8+ or activate venv." -ForegroundColor Red
        exit 1
    }
}

# Generate bindings
Write-Host "`nGenerating bindings..." -ForegroundColor Green
& $PythonExe -m grpc_tools.protoc `
    -I $ProtoDir `
    --python_out=$OutputDir `
    --grpc_python_out=$OutputDir `
    $ProtoFile

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Code generation failed" -ForegroundColor Red
    Write-Host "Install grpcio-tools: pip install grpcio-tools" -ForegroundColor Yellow
    exit 1
}

Write-Host "`n[SUCCESS] Generated:" -ForegroundColor Green
Write-Host "  - fluxgraph_pb2.py" -ForegroundColor White
Write-Host "  - fluxgraph_pb2_grpc.py" -ForegroundColor White
Write-Host "`nOutput directory: $OutputDir" -ForegroundColor White
