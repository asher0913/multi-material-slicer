# Build the STEP assembly converter (step_to_stl_parts.exe) from tools\step_to_stl_parts.py.
# The executable bundles the OCP/OpenCascade runtime from the CadQuery wheel.
param(
    [string]$Python = "",
    [string]$VenvDir = ""
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path "$PSScriptRoot\.."
if ([string]::IsNullOrWhiteSpace($VenvDir)) {
    $VenvDir = Join-Path $Root "backend_build\step_venv"
}

function Invoke-HostPython {
    param([string[]]$PythonArgs)

    if (-not [string]::IsNullOrWhiteSpace($Python)) {
        & $Python @PythonArgs
        return $LASTEXITCODE
    }

    $candidates = @(
        @("py", "-3.12"),
        @("py", "-3.11"),
        @("py", "-3.10"),
        @("python")
    )
    foreach ($candidate in $candidates) {
        $exe = $candidate[0]
        if (-not (Get-Command $exe -ErrorAction SilentlyContinue)) { continue }
        $prefix = @()
        if ($candidate.Count -gt 1) {
            $prefix = $candidate[1..($candidate.Count - 1)]
        }
        & $exe @prefix @PythonArgs
        if ($LASTEXITCODE -eq 0) { return 0 }
    }
    return 1
}

Push-Location $Root
if ((Invoke-HostPython @("-m", "venv", $VenvDir)) -ne 0) {
    throw "Could not create Python venv. CadQuery/OCP requires Python 3.10-3.12; install one of those versions or pass -Python C:\Path\To\python.exe."
}
$VenvPython = Join-Path $VenvDir "Scripts\python.exe"
if (-not (Test-Path $VenvPython)) {
    throw "Venv python not found at $VenvPython"
}
& $VenvPython -c "import sys; print('Python ' + sys.version.split()[0]); raise SystemExit(0 if sys.version_info.major == 3 and 10 <= sys.version_info.minor <= 12 else 7)"
if ($LASTEXITCODE -ne 0) {
    throw "STEP converter packaging requires Python 3.10, 3.11, or 3.12 because CadQuery/OCP wheels may not exist for newer Python versions."
}
& $VenvPython -m pip install --upgrade pip
& $VenvPython -m pip install "pyinstaller>=6,<7" -r requirements-step.txt
& $VenvPython -m PyInstaller --clean --onefile --name step_to_stl_parts `
    --hidden-import OCP.BRepMesh `
    --hidden-import OCP.IFSelect `
    --hidden-import OCP.STEPControl `
    --hidden-import OCP.StlAPI `
    --hidden-import OCP.TopAbs `
    --hidden-import OCP.TopExp `
    --collect-submodules OCP `
    --collect-binaries OCP `
    --distpath (Join-Path $Root "backend_dist") `
    --workpath (Join-Path $Root "backend_build\step_pyinstaller_work") `
    --specpath (Join-Path $Root "backend_build\step_pyinstaller_spec") `
    tools\step_to_stl_parts.py
Pop-Location

Write-Host "STEP converter written to: $(Join-Path $Root 'backend_dist\step_to_stl_parts.exe')"
