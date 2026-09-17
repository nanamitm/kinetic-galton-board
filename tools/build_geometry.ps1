<#
    Regenerates assets/geometry.json from the printable 3MF.

        pwsh tools/build_geometry.ps1
        pwsh tools/build_geometry.ps1 -Blender "C:\path\to\blender.exe"

    Step 1 unpacks the 3MF into STLs (Python, stdlib only).
    Step 2 runs Blender headless to cut the body at mid-depth, walk the cut
    edges into closed contours and measure the machine off them.

    The app reads the resulting JSON; it never touches the 3MF itself.
#>
[CmdletBinding()]
param(
    [string]$Project = (Split-Path -Parent $PSScriptRoot),
    [string]$Blender = "",
    [string]$Python  = "python",
    [switch]$NoPreview
)

$ErrorActionPreference = "Stop"

if (-not $Blender) {
    $candidates = @(
        (Get-Command blender -ErrorAction SilentlyContinue).Source
        Get-ChildItem "C:\Program Files\Blender Foundation" -Filter blender.exe `
            -Recurse -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | ForEach-Object FullName
    ) | Where-Object { $_ }
    if (-not $candidates) {
        throw "Blender not found. Pass -Blender <path to blender.exe>."
    }
    $Blender = @($candidates)[0]
}

$mf      = Join-Path $Project "MaxwellBoltzmann775.3mf"
$stlDir  = Join-Path $Project "build_assets\stl"
$outJson = Join-Path $Project "assets\geometry.json"
$preview = Join-Path $Project "docs\slice_preview.png"

if (-not (Test-Path $mf)) { throw "Cannot find $mf" }

Write-Host "[1/2] unpacking 3MF -> STL" -ForegroundColor Cyan
& $Python (Join-Path $PSScriptRoot "extract_3mf.py") $mf $stlDir
if ($LASTEXITCODE -ne 0) { throw "extract_3mf.py failed" }

Write-Host "[2/2] slicing in Blender  ($Blender)" -ForegroundColor Cyan
$blArgs = @("-b", "-P", (Join-Path $PSScriptRoot "blender_extract.py"), "--", $stlDir, $outJson)
if (-not $NoPreview) { $blArgs += $preview }
& $Blender @blArgs
if ($LASTEXITCODE -ne 0) { throw "blender_extract.py failed" }

Write-Host "geometry written to $outJson" -ForegroundColor Green
