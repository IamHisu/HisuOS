param(
    [ValidateSet('build', 'reconfigure', 'menuconfig', 'size')]
    [string]$Action = 'build',
    [string]$EimConfig = 'C:\Espressif\tools\eim_idf.json'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$config = Get-Content -LiteralPath $EimConfig -Raw | ConvertFrom-Json
$idf = $config.idfInstalled | Where-Object name -eq 'v5.5.5' | Select-Object -First 1
if (!$idf) { throw 'Install ESP-IDF v5.5.5 with EIM first.' }

$previous = @{}
try {
    # -e exports environment only: no global EIM selection or profile activation.
    $lines = & $idf.activationScript -e 6>&1
    foreach ($entry in $lines) {
        $line = "$entry"
        if ($line -notmatch '^([A-Z_][A-Z0-9_]*)=(.*)$') { continue }
        $key = $Matches[1]
        $value = $Matches[2]
        if ($key -eq 'SYSTEM_PATH') { continue }
        $previous[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
        if ($key -eq 'PATH') { $value += ';' + $previous[$key] }
        [Environment]::SetEnvironmentVariable($key, $value, 'Process')
    }
    $idfPy = Join-Path $idf.path 'tools\idf.py'
    $version = & $idf.python $idfPy --version 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or "$version" -notmatch 'ESP-IDF v5\.5\.5(?:\s|$)') {
        throw "Expected ESP-IDF v5.5.5, got: $version"
    }
    Push-Location $root
    try {
        & $idf.python $idfPy '-DIDF_TARGET=esp32s3' $Action
        if ($LASTEXITCODE -ne 0) { throw "ESP-IDF $Action failed ($LASTEXITCODE)." }
    } finally { Pop-Location }
} finally {
    foreach ($key in $previous.Keys) {
        [Environment]::SetEnvironmentVariable($key, $previous[$key], 'Process')
    }
}
