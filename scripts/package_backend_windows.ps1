# Build the backend command-line tool (slice_merge_tool.exe) from slice_1080p.py
# using PyInstaller, so the App does not depend on a Python install at runtime.
param(
    [string]$Python = "",
    [string]$VenvDir = ""
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path "$PSScriptRoot\.."
if ([string]::IsNullOrWhiteSpace($VenvDir)) {
    $VenvDir = Join-Path $Root "backend_build\venv"
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
    throw "Could not create Python venv. Install Python 3.10-3.12, or pass -Python C:\Path\To\python.exe."
}
$VenvPython = Join-Path $VenvDir "Scripts\python.exe"
if (-not (Test-Path $VenvPython)) {
    throw "Venv python not found at $VenvPython"
}
& $VenvPython -c "import sys; print('Python ' + sys.version.split()[0])"
& $VenvPython -m pip install --upgrade pip
& $VenvPython -m pip install "pyinstaller>=6,<7"
& $VenvPython -m pip install -r requirements.txt
& $VenvPython -m PyInstaller --clean --onefile --name slice_merge_tool `
    --hidden-import yaml `
    --collect-submodules yaml `
    --distpath (Join-Path $Root "backend_dist") `
    --workpath (Join-Path $Root "backend_build") `
    --specpath (Join-Path $Root "backend_build") `
    slice_1080p.py
Pop-Location

Write-Host "Backend tool written to: $(Join-Path $Root 'backend_dist\slice_merge_tool.exe')"
Write-Host "It still supports:  slice_merge_tool.exe --config <config.yaml> --output <merged_dir>"
