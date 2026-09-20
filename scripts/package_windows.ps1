# =====================================================================
# MultiMaterialSlicer - Windows packaging (one stop)
#
# Builds:
#   1. The backend tool  slice_merge_tool.exe  (PyInstaller, no Python needed at runtime)
#   2. The STEP converter  step_to_stl_parts.exe  (PyInstaller + CadQuery/OCP)
#   3. The Qt application MultiMaterialSlicer.exe
#   4. Collects Qt DLLs (windeployqt), embeds tools + config + examples
#   5. Produces dist\MultiMaterialSlicer-win64.zip  (give ONLY this to customers)
#
# Usage (or just double-click build_windows.bat):
#   powershell -ExecutionPolicy Bypass -File scripts\package_windows.ps1
#   ...optional overrides:
#   -QtPrefix C:\Qt\5.12.12\msvc2017_64  -Python C:\Python312\python.exe  -RunSelfTest
# =====================================================================
param(
    [string]$QtPrefix = "",
    [string]$Generator = "",
    [string]$Python = "",
    [switch]$SkipBackend,
    [switch]$SkipStepTool,
    [switch]$RunSelfTest
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..").Path
$BuildDir = Join-Path $Root "build-win-release"
$DistRoot = Join-Path $Root "dist"
$Stage = Join-Path $DistRoot "MultiMaterialSlicer-win64"
$Zip = Join-Path $DistRoot "MultiMaterialSlicer-win64.zip"

function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }
function Info($msg) { Write-Host $msg -ForegroundColor Cyan }

# ---- Locate Qt -------------------------------------------------------
function Resolve-Qt {
    param([string]$hint)
    if ($hint -and (Test-Path (Join-Path $hint "bin\windeployqt.exe"))) { return $hint }
    $cands = @(
        "C:\Qt\5.12.12\msvc2017_64",
        "C:\Qt\5.12.12\msvc2019_64",
        "C:\Qt\5.12.12\msvc2017",
        "C:\Qt5.12.12\5.12.12\msvc2017_64"
    )
    foreach ($c in $cands) { if (Test-Path (Join-Path $c "bin\windeployqt.exe")) { return $c } }
    if (Test-Path "C:\Qt\5.12.12") {
        $f = Get-ChildItem "C:\Qt\5.12.12" -Directory -ErrorAction SilentlyContinue |
             Where-Object { Test-Path (Join-Path $_.FullName "bin\windeployqt.exe") } |
             Select-Object -First 1
        if ($f) { return $f.FullName }
    }
    return $null
}

$QtPrefix = Resolve-Qt $QtPrefix
if (-not $QtPrefix) {
    Fail "Could not find Qt 5.12.12 MSVC. Install it (e.g. C:\Qt\5.12.12\msvc2017_64) or pass -QtPrefix <path>."
}
Info "Qt:        $QtPrefix"

# ---- Locate CMake ----------------------------------------------------
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Fail "cmake not found on PATH. Install CMake and reopen the terminal."
}

# ---- Pick a Visual Studio generator ---------------------------------
if (-not $Generator) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $ver = & $vswhere -latest -property catalog_productLineVersion 2>$null
        if ($ver -eq "2022") { $Generator = "Visual Studio 17 2022" }
        elseif ($ver -eq "2019") { $Generator = "Visual Studio 16 2019" }
    }
    if (-not $Generator) { $Generator = "Visual Studio 17 2022" }  # sensible default
}
Info "Generator: $Generator"

# ---- 1. Backend tool (PyInstaller) ----------------------------------
$BackendExe = Join-Path $Root "backend_dist\slice_merge_tool.exe"
$StepExe = Join-Path $Root "backend_dist\step_to_stl_parts.exe"
if ($SkipBackend) {
    Info "Skipping backend build (-SkipBackend)."
} else {
    Info "==> Building backend tool (slice_merge_tool.exe) ..."
    $bargs = @()
    if ($Python) { $bargs += @("-Python", $Python) }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "package_backend_windows.ps1") @bargs
    if ($LASTEXITCODE -ne 0) { Fail "Backend build failed. Is Python 3 installed and on PATH?" }
}
if (-not (Test-Path $BackendExe)) {
    Write-Host "WARNING: $BackendExe not found." -ForegroundColor Yellow
    Write-Host "         The app will be built without an embedded backend (dev mode only)." -ForegroundColor Yellow
}

if ($SkipStepTool) {
    Info "Skipping STEP converter build (-SkipStepTool)."
} else {
    Info "==> Building STEP converter (step_to_stl_parts.exe) ..."
    $sargs = @()
    if ($Python) { $sargs += @("-Python", $Python) }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "package_step_windows.ps1") @sargs
    if ($LASTEXITCODE -ne 0) { Fail "STEP converter build failed. Is Python 3 installed and on PATH?" }
}
if (-not (Test-Path $StepExe)) {
    Write-Host "WARNING: $StepExe not found. STEP import will require Python + OCP/CadQuery at runtime." -ForegroundColor Yellow
}

# ---- 2. Configure + build the app -----------------------------------
# Configure AFTER the backend exists so CMake embeds it next to the exe.
Info "==> Configuring (CMake) ..."
cmake -S $Root -B $BuildDir -G $Generator -A x64 -DCMAKE_PREFIX_PATH=$QtPrefix
if ($LASTEXITCODE -ne 0) { Fail "CMake configure failed." }

Info "==> Building (Release) ..."
cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) { Fail "Build failed." }

$ExeDir = Join-Path $BuildDir "Release"
$Exe = Join-Path $ExeDir "MultiMaterialSlicer.exe"
if (-not (Test-Path $Exe)) { Fail "Built exe not found at $Exe" }

# ---- 3. Assemble the distributable folder ---------------------------
Info "==> Assembling package ..."
if (Test-Path $Stage) { Remove-Item $Stage -Recurse -Force }
New-Item -ItemType Directory -Path $Stage | Out-Null

# Copy the exe and everything CMake placed next to it (backend exe, config, script).
Copy-Item (Join-Path $ExeDir "*") $Stage -Recurse -Force

# Collect Qt runtime DLLs and plugins into the package folder.
& (Join-Path $QtPrefix "bin\windeployqt.exe") --release --no-translations (Join-Path $Stage "MultiMaterialSlicer.exe")
if ($LASTEXITCODE -ne 0) { Fail "windeployqt failed." }

# Make sure backend + config are present (defensive; CMake already copies them).
if (Test-Path $BackendExe) { Copy-Item $BackendExe (Join-Path $Stage "slice_merge_tool.exe") -Force }
if (Test-Path $StepExe) { Copy-Item $StepExe (Join-Path $Stage "step_to_stl_parts.exe") -Force }
Copy-Item (Join-Path $Root "config\machine_material_presets.yaml") $Stage -Force

# Include demo files for meeting/customer smoke tests. They are small and make a
# clean Windows machine immediately demonstrable after unzipping the package.
$ExamplesDir = Join-Path $Stage "examples"
New-Item -ItemType Directory -Path $ExamplesDir -Force | Out-Null
if (Test-Path (Join-Path $Root "demo_stl")) {
    Copy-Item (Join-Path $Root "demo_stl") (Join-Path $ExamplesDir "demo_stl") -Recurse -Force
}
if (Test-Path (Join-Path $Root "demo_step")) {
    Copy-Item (Join-Path $Root "demo_step") (Join-Path $ExamplesDir "demo_step") -Recurse -Force
}

# Smoke-check packaged command-line helpers and critical runtime files before
# zipping, so a missing backend/STEP tool is caught on the build machine.
$RequiredFiles = @(
    "MultiMaterialSlicer.exe",
    "machine_material_presets.yaml",
    "platforms\qwindows.dll"
)
if (-not $SkipBackend) { $RequiredFiles += "slice_merge_tool.exe" }
if (-not $SkipStepTool) { $RequiredFiles += "step_to_stl_parts.exe" }
foreach ($rel in $RequiredFiles) {
    $path = Join-Path $Stage $rel
    if (-not (Test-Path $path)) { Fail "Required packaged file is missing: $path" }
}

if (-not $SkipBackend) {
    & (Join-Path $Stage "slice_merge_tool.exe") --help | Out-Null
    if ($LASTEXITCODE -ne 0) { Fail "Packaged slice_merge_tool.exe failed to start." }
}
if (-not $SkipStepTool) {
    & (Join-Path $Stage "step_to_stl_parts.exe") --help | Out-Null
    if ($LASTEXITCODE -ne 0) { Fail "Packaged step_to_stl_parts.exe failed to start." }
}

if ($RunSelfTest) {
    Info "==> Running packaged app self-test ..."
    $stepExample = Join-Path $Stage "examples\demo_step\two-part-assembly.step"
    $stlA = Join-Path $Stage "examples\demo_stl\multi_A_base_plate.stl"
    $stlB = Join-Path $Stage "examples\demo_stl\multi_B_cross_insert.stl"
    if ((Test-Path $stepExample) -and -not $SkipStepTool) {
        & (Join-Path $Stage "MultiMaterialSlicer.exe") --selftest $stepExample
    } elseif ((Test-Path $stlA) -and (Test-Path $stlB)) {
        & (Join-Path $Stage "MultiMaterialSlicer.exe") --selftest $stlA $stlB
    } else {
        Fail "No packaged examples found for self-test."
    }
    if ($LASTEXITCODE -ne 0) { Fail "Packaged app self-test failed." }
}

# ---- 4. Zip ----------------------------------------------------------
Info "==> Creating zip ..."
if (Test-Path $Zip) { Remove-Item $Zip -Force }
Compress-Archive -Path $Stage -DestinationPath $Zip -Force

Write-Host ""
Write-Host "Package: $Zip" -ForegroundColor Green
Write-Host "Folder : $Stage" -ForegroundColor Green
Write-Host "Distribute the zip; the customer just unzips and runs MultiMaterialSlicer.exe (no Qt/Python needed)."
