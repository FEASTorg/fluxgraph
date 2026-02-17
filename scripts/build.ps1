# FluxGraph Build Script (Windows)
#
# Usage:
#   .\scripts\build.ps1 [-Clean] [-Config <Release|Debug|RelWithDebInfo>] [-Server] [-NoTests] [-JSON] [-YAML]

param(
    [switch]$Clean,
    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string]$Config = "Release",
    [switch]$Server,
    [switch]$NoTests,
    [switch]$JSON,
    [switch]$YAML
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

Write-Host "============================================" -ForegroundColor Cyan
Write-Host "FluxGraph Build Script" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "Config:      $Config" -ForegroundColor White
Write-Host "Server:      $Server" -ForegroundColor White
Write-Host "Tests:       $(-not $NoTests)" -ForegroundColor White
Write-Host "JSON:        $JSON" -ForegroundColor White
Write-Host "YAML:        $YAML" -ForegroundColor White
Write-Host "Clean:       $Clean" -ForegroundColor White
Write-Host "============================================" -ForegroundColor Cyan

# Build directory naming
$BuildDirSuffix = $Config.ToLower()
if ($Server) { $BuildDirSuffix += "-server" }
$BuildDir = Join-Path $RepoRoot "build-$BuildDirSuffix"

# Clean if requested
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "`n[CLEAN] Removing $BuildDir..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

# Create build directory
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
Push-Location $BuildDir

try {
    # CMake configuration
    Write-Host "`n[CMAKE] Configuring..." -ForegroundColor Green
    
    $CMakeArgs = @(
        "-G", "Visual Studio 17 2022",
        "-A", "x64",
        "-DCMAKE_BUILD_TYPE=$Config"
    )
    
    # vcpkg toolchain
    if ($env:VCPKG_ROOT) {
        $ToolchainFile = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
        $CMakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile"
        Write-Host "  Using vcpkg: $env:VCPKG_ROOT" -ForegroundColor Gray
    }
    
    # Build options
    if (-not $NoTests) {
        $CMakeArgs += "-DFLUXGRAPH_BUILD_TESTS=ON"
    } else {
        $CMakeArgs += "-DFLUXGRAPH_BUILD_TESTS=OFF"
    }
    
    if ($Server) {
        $CMakeArgs += "-DFLUXGRAPH_BUILD_SERVER=ON"
    }
    
    if ($JSON) {
        $CMakeArgs += "-DFLUXGRAPH_JSON_ENABLED=ON"
    }
    
    if ($YAML) {
        $CMakeArgs += "-DFLUXGRAPH_YAML_ENABLED=ON"
    }
    
    $CMakeArgs += ".."
    
    & cmake @CMakeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed"
    }
    
    # Build
    Write-Host "`n[BUILD] Compiling ($Config)..." -ForegroundColor Green
    & cmake --build . --config $Config -j
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed"
    }
    
    # Run tests if enabled
    if (-not $NoTests) {
        Write-Host "`n[TEST] Running tests..." -ForegroundColor Green
        & ctest -C $Config --output-on-failure
        if ($LASTEXITCODE -ne 0) {
            Write-Host "`n[WARNING] Some tests failed" -ForegroundColor Yellow
        } else {
            Write-Host "`n[SUCCESS] All tests passed!" -ForegroundColor Green
        }
    }
    
    Write-Host "`n============================================" -ForegroundColor Cyan
    Write-Host "Build completed successfully!" -ForegroundColor Green
    Write-Host "Build directory: $BuildDir" -ForegroundColor White
    Write-Host "============================================" -ForegroundColor Cyan
    
} catch {
    Write-Host "`n[ERROR] Build failed: $_" -ForegroundColor Red
    exit 1
} finally {
    Pop-Location
}
