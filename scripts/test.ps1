# FluxGraph Test Script (Windows)
#
# Usage:
#   .\scripts\test.ps1 [-Config <Release|Debug|RelWithDebInfo>] [-Integration] [-Verbose]

param(
    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string]$Config = "Release",
    [switch]$Integration,
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

Write-Host "============================================" -ForegroundColor Cyan
Write-Host "FluxGraph Test Runner" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan

# Find build directory
$BuildDirs = @(
    "build-$($Config.ToLower())-server",
    "build-$($Config.ToLower())",
    "build-server",
    "build-core",
    "build-json",
    "build-yaml",
    "build-both"
)

$BuildDir = $null
foreach ($dir in $BuildDirs) {
    $path = Join-Path $RepoRoot $dir
    if (Test-Path $path) {
        $BuildDir = $path
        break
    }
}

if (-not $BuildDir) {
    Write-Host "[ERROR] No build directory found. Run build.ps1 first." -ForegroundColor Red
    exit 1
}

Write-Host "Build dir:   $BuildDir" -ForegroundColor White
Write-Host "Integration: $Integration" -ForegroundColor White
Write-Host "============================================" -ForegroundColor Cyan

Push-Location $BuildDir

try {
    # Run C++ unit tests
    Write-Host "`n[TEST] Running C++ unit tests..." -ForegroundColor Green
    
    $CTestArgs = @("-C", $Config, "--output-on-failure")
    if ($Verbose) {
        $CTestArgs += "-V"
    }
    
    & ctest @CTestArgs
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "`n[FAILED] Some C++ tests failed" -ForegroundColor Red
        exit 1
    }
    
    Write-Host "`n[SUCCESS] C++ tests passed!" -ForegroundColor Green
    
    # Run integration tests if requested
    if ($Integration) {
        Write-Host "`n[TEST] Running integration tests..." -ForegroundColor Green
        
        # Check for venv
        $VenvPath = Join-Path $RepoRoot ".venv-fxg"
        if (-not (Test-Path $VenvPath)) {
            Write-Host "[WARNING] Python venv not found at: $VenvPath" -ForegroundColor Yellow
            Write-Host "  Create venv: python -m venv .venv-fxg" -ForegroundColor Yellow
            Write-Host "  Install deps: .venv-fxg\Scripts\pip install -r requirements.txt" -ForegroundColor Yellow
            exit 0
        }
        
        # Ensure protobuf bindings are generated
        $ProtoPythonDir = Join-Path $RepoRoot "build-server\python"
        $ProtoFiles = Get-ChildItem -Path $ProtoPythonDir -Filter "*_pb2*.py" -ErrorAction SilentlyContinue
        if (-not $ProtoFiles) {
            Write-Host "  Generating Python protobuf bindings..." -ForegroundColor Gray
            $GenScript = Join-Path $RepoRoot "scripts\generate_proto_python.ps1"
            & $GenScript
        }
        
        # Find server executable
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
            Write-Host "[WARNING] Server not built. Build with: .\scripts\build.ps1 -Server -Config $Config" -ForegroundColor Yellow
            exit 0
        }
        
        # Start server in background
        Write-Host "  Starting server: $ServerExe" -ForegroundColor Gray
        $ServerProc = Start-Process -FilePath $ServerExe -ArgumentList "--port 50051" -WindowStyle Hidden -PassThru
        
        try {
            Start-Sleep -Seconds 2
            
            # Run Python integration tests
            $PythonExe = Join-Path $VenvPath "Scripts\python.exe"
            $TestScript = Join-Path $RepoRoot "tests\test_grpc_integration.py"
            
            & $PythonExe $TestScript
            
            if ($LASTEXITCODE -ne 0) {
                Write-Host "`n[FAILED] Integration tests failed" -ForegroundColor Red
                exit 1
            }
            
            Write-Host "`n[SUCCESS] Integration tests passed!" -ForegroundColor Green
            
        } finally {
            # Stop server
            Stop-Process -Id $ServerProc.Id -Force -ErrorAction SilentlyContinue
        }
    }
    
    Write-Host "`n============================================" -ForegroundColor Cyan
    Write-Host "All tests passed!" -ForegroundColor Green
    Write-Host "============================================" -ForegroundColor Cyan
    
} catch {
    Write-Host "`n[ERROR] Test run failed: $_" -ForegroundColor Red
    exit 1
} finally {
    Pop-Location
}
