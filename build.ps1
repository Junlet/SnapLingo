#Requires -Version 5.1
<#
.SYNOPSIS
    Build SnapLingo and package it into a Windows installer.

.DESCRIPTION
    Runs the whole release pipeline:

      1. Configure CMake with a Visual Studio x64 generator against Qt 6 (MSVC)
      2. Build the Release configuration into build\Release
      3. Collect Qt plugins and the MSVC runtime with windeployqt
      4. Compile setup.iss with Inno Setup into setup\SnapLingo_Setup.exe

    The Visual Studio generator is used on purpose: setup.iss expects the build
    output at build\Release, and that layout is what a multi-config generator
    produces.

.PARAMETER QtDir
    Qt 6 MSVC x64 installation, e.g. C:\Qt\6.8.3\msvc2022_64

.PARAMETER Generator
    CMake generator name. Defaults to the newest Visual Studio generator that
    this machine has the C++ toolset for.

.PARAMETER SkipDeploy
    Skip windeployqt. The produced exe will not start without the Qt DLLs.

.PARAMETER SkipInstaller
    Stop after the deployed build; do not produce the installer.

.EXAMPLE
    .\build.ps1

.EXAMPLE
    .\build.ps1 -QtDir C:\Qt\6.10.3\msvc2022_64 -SkipInstaller
#>
[CmdletBinding()]
param(
    [string]$QtDir = 'C:\Qt\6.8.3\msvc2022_64',
    [string]$Generator = '',
    [switch]$SkipDeploy,
    [switch]$SkipInstaller
)

$ErrorActionPreference = 'Stop'

$root   = $PSScriptRoot
$build  = Join-Path $root 'build'
$appExe = 'SnapLingo.exe'
$release = Join-Path $build 'Release'

function Write-Step([string]$text) {
    Write-Host ''
    Write-Host "==> $text" -ForegroundColor Cyan
}

# Native tools (cmake, windeployqt, ISCC) write progress and warnings to stderr.
# With $ErrorActionPreference = 'Stop' PowerShell 5.1 turns the first stderr line
# into a terminating NativeCommandError, which aborts a perfectly good build.
# This wrapper merges the streams, prints everything, and judges the exit code.
function Invoke-Native {
    param(
        [Parameter(Mandatory)][string]$Exe,
        [string[]]$Arguments = @(),
        [Parameter(Mandatory)][string]$What
    )
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Exe @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    if ($code -ne 0) { throw "$What failed (exit $code)." }
}

function Find-VsInstall {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $path = & $vswhere -products * -latest `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null
    if ($path) { return $path.Trim() }
    return $null
}

function Find-CMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $candidates = @(
        (Join-Path $env:APPDATA 'Python\Python314\Scripts\cmake.exe'),
        'C:\Program Files\CMake\bin\cmake.exe'
    )
    $vs = Find-VsInstall
    if ($vs) {
        $candidates += (Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
    }
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { return $c }
    }
    throw 'CMake not found. Install it with:  pip install --user cmake'
}

function Resolve-Generator([string]$cmake) {
    if ($Generator) { return $Generator }

    $help = (& $cmake --help 2>&1 | Out-String)
    # Newest first; the first one present in `cmake --help` is what we use.
    foreach ($name in @('Visual Studio 18 2026', 'Visual Studio 17 2022', 'Visual Studio 16 2019')) {
        if ($help -match [regex]::Escape($name)) { return $name }
    }
    throw 'No Visual Studio CMake generator found. Run from a VS developer prompt or pass -Generator.'
}

# ---------------------------------------------------------------- preflight
if (-not (Test-Path (Join-Path $root 'CMakeLists.txt'))) {
    throw "CMakeLists.txt not found next to this script ($root)."
}

$windeployqt = Join-Path $QtDir 'bin\windeployqt.exe'
$qtConfig    = Join-Path $QtDir 'lib\cmake\Qt6\Qt6Config.cmake'
if (-not (Test-Path $qtConfig)) {
    throw "Qt 6 not found at '$QtDir'. Pass -QtDir <...\Qt\6.x.y\msvc2022_64>."
}
if (-not $SkipDeploy -and -not (Test-Path $windeployqt)) {
    throw "windeployqt not found at '$windeployqt'. Is -QtDir pointing at a full Qt install?"
}

$cmake = Find-CMake
$gen   = Resolve-Generator $cmake
$vs    = Find-VsInstall

Write-Host "project   : $root"
Write-Host "qt        : $QtDir"
Write-Host "cmake     : $cmake"
Write-Host "generator : $gen"
Write-Host "msvc      : $(if ($vs) { $vs } else { '<not detected>' })"

# ---------------------------------------------------------------- configure
Write-Step 'Configuring CMake'
Invoke-Native $cmake @('-S', $root, '-B', $build, '-G', $gen, '-A', 'x64', "-DCMAKE_PREFIX_PATH=$QtDir\lib\cmake") 'CMake configure'

# -------------------------------------------------------------------- build
Write-Step 'Building Release'
Invoke-Native $cmake @('--build', $build, '--config', 'Release', '--parallel') 'Build'

$exePath = Join-Path $release $appExe
if (-not (Test-Path $exePath)) { throw "Expected binary not found: $exePath" }
Write-Host "built: $exePath"

# ------------------------------------------------------------------- deploy
if (-not $SkipDeploy) {
    Write-Step 'Deploying Qt runtime (windeployqt)'

    # windeployqt --compiler-runtime needs to locate the MSVC redistributables.
    if ($vs) {
        $env:VCINSTALLDIR = Join-Path $vs 'VC\'
        $redistRoot = Join-Path $vs 'VC\Redist\MSVC'
        if (Test-Path $redistRoot) {
            $newest = Get-ChildItem $redistRoot -Directory |
                      Sort-Object Name -Descending | Select-Object -First 1
            if ($newest) { $env:VCToolsRedistDir = $newest.FullName + '\' }
        }
    }

    Invoke-Native $windeployqt @('--release', '--no-translations', '--no-opengl-sw', '--compiler-runtime', $exePath) 'windeployqt'

    $qwindows = Join-Path $release 'platforms\qwindows.dll'
    if (-not (Test-Path $qwindows)) {
        Write-Warning "platforms\qwindows.dll is missing - the app will not start. Re-run windeployqt manually."
    } else {
        Write-Host 'Qt platform plugin deployed.'
    }
}

# CMake POST_BUILD already copies assets/ and WebView2Loader.dll; verify anyway.
foreach ($required in @('assets', 'WebView2Loader.dll')) {
    $p = Join-Path $release $required
    if (-not (Test-Path $p)) {
        Write-Warning "Missing in build\Release: $required (CMake post-build step may have failed)."
    }
}

# ---------------------------------------------------------------- installer
if (-not $SkipInstaller) {
    Write-Step 'Compiling installer (Inno Setup)'

    # The app stores translations in <exe dir>\storage by default, so simply running
    # the built exe (a smoke test, for example) creates a storage\ folder here. It is
    # user data, never payload: shipping it would overwrite the installer target's
    # existing history.db. Remove it before Inno Setup packs the build.
    $strayStorage = Join-Path $release 'storage'
    if (Test-Path $strayStorage) {
        Remove-Item $strayStorage -Recurse -Force
        Write-Host 'Removed build\Release\storage (runtime data, not payload).'
    }


    $isccCandidates = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe')
    )
    $iscc = $isccCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $iscc) {
        throw 'Inno Setup (ISCC.exe) not found. Install it, then re-run with -SkipInstaller to build only.'
    }

    Push-Location $root
    try {
        Invoke-Native $iscc @( (Join-Path $root 'setup.iss') ) 'Inno Setup'
    } finally {
        Pop-Location
    }

    $installer = Join-Path $root 'setup\SnapLingo_Setup.exe'
    if (Test-Path $installer) {
        $mb = [math]::Round((Get-Item $installer).Length / 1MB, 2)
        Write-Host ''
        Write-Host "installer : $installer ($mb MB)" -ForegroundColor Green
    }
}

Write-Step 'Done'
$releaseMb = [math]::Round(((Get-ChildItem $release -Recurse -File | Measure-Object Length -Sum).Sum / 1MB), 2)
Write-Host "deployed build : $release ($releaseMb MB)"
