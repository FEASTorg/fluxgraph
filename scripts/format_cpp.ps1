<#
.SYNOPSIS
    Run clang-format over C++ sources.

.DESCRIPTION
    Formats selected directories relative to repo root.
    Defaults to all if no switches are provided.
#>

param(
    [switch]$Examples,
    [switch]$Tests,
    [switch]$Server,
    [switch]$Src,
    [switch]$Include
)

$ErrorActionPreference = "Stop"

# Resolve repo root (script located in scripts/)
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

$Dirs = @{
    Examples = Join-Path $RepoRoot "examples"
    Tests    = Join-Path $RepoRoot "tests"
    Server   = Join-Path $RepoRoot "server"
    Src      = Join-Path $RepoRoot "src"
    Include  = Join-Path $RepoRoot "include"
}

# Determine targets (default: all)
$Targets = if (-not ($Examples -or $Tests -or $Server -or $Src -or $Include)) {
    $Dirs.Values
} else {
    @(
        if ($Examples) { $Dirs.Examples }
        if ($Tests)    { $Dirs.Tests }
        if ($Server)   { $Dirs.Server }
        if ($Src)      { $Dirs.Src }
        if ($Include)  { $Dirs.Include }
    )
}

$Total = 0

foreach ($dir in $Targets) {
    if (Test-Path $dir) {
        $Files = Get-ChildItem $dir -Recurse -Include *.cpp,*.hpp -File
        if ($Files) {
            Write-Host "Formatting $($Files.Count) file(s) in $dir"
            $Files | ForEach-Object { clang-format -i $_.FullName }
            $Total += $Files.Count
        }
    }
}

Write-Host ""
Write-Host "Formatted $Total file(s)."
