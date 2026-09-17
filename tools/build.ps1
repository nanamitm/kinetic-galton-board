<#
    Configures and builds the Qt6 application.

        pwsh tools/build.ps1
        pwsh tools/build.ps1 -Run
        pwsh tools/build.ps1 -QtDir C:\Qt\6.11.1\msvc2022_64
        pwsh tools/build.ps1 -Kit msvc2022_64

    With no arguments it picks the newest Qt 6 kit under C:\Qt matching -Kit
    (default mingw_64), plus the CMake and Ninja that ship with the Qt
    installer, so no Visual Studio environment is needed.  CMAKE_PREFIX_PATH
    or Qt6_DIR in the environment take precedence.

    On Linux/macOS use CMake directly, or the presets in CMakePresets.json.
#>
[CmdletBinding()]
param(
    [string]$Project   = (Split-Path -Parent $PSScriptRoot),
    [string]$QtDir     = "",
    [string]$QtRoot    = "C:\Qt",
    [string]$Kit       = "mingw_64",
    [string]$CMake     = "",
    [string]$BuildType = "Release",
    [switch]$Clean,
    [switch]$Run
)

$ErrorActionPreference = "Stop"

# --- locate Qt -------------------------------------------------------------
if (-not $QtDir) { $QtDir = $env:CMAKE_PREFIX_PATH }
if (-not $QtDir -and $env:Qt6_DIR) { $QtDir = $env:Qt6_DIR }
if (-not $QtDir -and (Test-Path $QtRoot)) {
    $QtDir = @(
        Get-ChildItem $QtRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+\.\d+' } |
            Sort-Object { [version]$_.Name } -Descending |
            ForEach-Object { Join-Path $_.FullName $Kit } |
            Where-Object { Test-Path (Join-Path $_ "lib\cmake\Qt6") }
    )[0]
}
if (-not $QtDir) {
    throw "Qt 6 not found. Pass -QtDir <kit path, e.g. C:\Qt\6.11.1\mingw_64>."
}

# --- locate CMake ----------------------------------------------------------
if (-not $CMake) {
    $bundled = @(
        Get-ChildItem (Join-Path $QtRoot "Tools") -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like "CMake*" } |
            ForEach-Object { Join-Path $_.FullName "bin\cmake.exe" } |
            Where-Object { Test-Path $_ }
    )
    $CMake = if ($bundled) { $bundled[0] } else { (Get-Command cmake -ErrorAction SilentlyContinue).Source }
}
if (-not $CMake) { throw "cmake not found." }

# --- the MinGW kit needs its compiler and ninja on PATH --------------------
$ninja = Join-Path $QtRoot "Tools\Ninja"
if (Test-Path $ninja) { $env:PATH = "$ninja;$env:PATH" }
if ($QtDir -like "*mingw*") {
    $mingw = @(
        Get-ChildItem (Join-Path $QtRoot "Tools") -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like "mingw*" } |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName "bin" } |
            Where-Object { Test-Path $_ }
    )
    if ($mingw) { $env:PATH = "$($mingw[0]);$env:PATH" }
}

Write-Host "Qt:    $QtDir" -ForegroundColor DarkGray
Write-Host "CMake: $CMake" -ForegroundColor DarkGray

# --- configure and build ---------------------------------------------------
$build = Join-Path $Project "build"
if ($Clean -and (Test-Path $build)) { Remove-Item $build -Recurse -Force }

$cfgArgs = @(
    "-S", $Project
    "-B", $build
    "-G", "Ninja"
    "-DCMAKE_PREFIX_PATH=$QtDir"
    "-DCMAKE_BUILD_TYPE=$BuildType"
)
& $CMake @cfgArgs
if ($LASTEXITCODE -ne 0) { throw "configure failed" }

& $CMake --build $build
if ($LASTEXITCODE -ne 0) { throw "build failed" }

$exe = Join-Path $build "KineticGaltonBoard.exe"
Write-Host "built $exe" -ForegroundColor Green

if ($Run) {
    $env:PATH = "$QtDir\bin;$env:PATH"
    & $exe
}
