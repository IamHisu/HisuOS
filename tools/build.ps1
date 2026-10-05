param(
    [string]$ActivationScript,
    [ValidateSet('build', 'reconfigure', 'menuconfig', 'size')]
    [string]$Action = 'build'
)

$ErrorActionPreference = 'Stop'

function Test-HisuIdfVersion([string]$Path) {
    if (-not $Path) { return $false }
    $taskVersionFile = Join-Path $Path 'tools/cmake/version.cmake'
    if (-not (Test-Path -LiteralPath $taskVersionFile)) { return $false }
    $taskVersionText = Get-Content -LiteralPath $taskVersionFile -Raw
    $taskParts = foreach ($taskPart in @('MAJOR', 'MINOR', 'PATCH')) {
        [regex]::Match($taskVersionText, "set\(IDF_VERSION_$taskPart\s+(\d+)\)").Groups[1].Value
    }
    return ($taskParts -join '.') -eq '5.5.5'
}

if ($ActivationScript) {
    . $ActivationScript
} elseif (-not (Test-HisuIdfVersion $env:IDF_PATH) -or
          -not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    $taskEimConfig = 'C:/Espressif/tools/eim_idf.json'
    $taskSelected = $null
    if (Test-Path -LiteralPath $taskEimConfig) {
        $taskInstallations = Get-Content -LiteralPath $taskEimConfig -Raw | ConvertFrom-Json
        $taskSelected = $taskInstallations.idfInstalled |
            Where-Object { $_.name -eq 'v5.5.5' -and (Test-HisuIdfVersion $_.path) } |
            Select-Object -First 1
    }
    if ($taskSelected -and (Test-Path -LiteralPath $taskSelected.activationScript)) {
        . $taskSelected.activationScript
    } else {
        $taskCandidates = @(
            (Join-Path (Split-Path -Parent $PSScriptRoot) '.cache/esp-idf-v5.5.5'),
            'C:/esp/v5.5.5/esp-idf'
        )
        $taskIdfSource = $taskCandidates | Where-Object { Test-HisuIdfVersion $_ } | Select-Object -First 1
        if (-not $taskIdfSource) { throw 'Activate ESP-IDF 5.5.5 or pass -ActivationScript.' }
        if (-not $env:IDF_TOOLS_PATH) { $env:IDF_TOOLS_PATH = 'C:/Espressif/tools' }
        $taskPythonEnv = Join-Path $env:IDF_TOOLS_PATH 'python/v5.5.5/venv'
        if (-not (Test-Path -LiteralPath (Join-Path $taskPythonEnv 'Scripts/python.exe'))) {
            throw 'No ESP-IDF 5.5.5 Python environment. Install it or pass -ActivationScript.'
        }
        $env:IDF_PYTHON_ENV_PATH = $taskPythonEnv
        # EIM stores binaries directly in its tools directory; idf_tools expects a parent.
        if ((Test-Path -LiteralPath (Join-Path $env:IDF_TOOLS_PATH 'xtensa-esp-elf')) -and
            -not (Test-Path -LiteralPath (Join-Path $env:IDF_TOOLS_PATH 'tools/xtensa-esp-elf'))) {
            $env:IDF_TOOLS_PATH = Split-Path -Parent $env:IDF_TOOLS_PATH
        }
        . (Join-Path $taskPythonEnv 'Scripts/Activate.ps1')
        . (Join-Path $taskIdfSource 'export.ps1')
    }
}

if (-not (Test-HisuIdfVersion $env:IDF_PATH)) {
    throw 'HisuOS requires ESP-IDF 5.5.5. The activated source has a different version.'
}

Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    idf.py $Action
    if ($LASTEXITCODE -ne 0) { throw "ESP-IDF $Action failed ($LASTEXITCODE)." }
} finally {
    Pop-Location
}
