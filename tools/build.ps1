param(
    [ValidateSet('build', 'reconfigure', 'menuconfig', 'size')]
    [string]$Action = 'build',
    [string]$EimConfig = 'C:\Espressif\tools\eim_idf.json',
    [string]$BuildDirectory = 'build-idf61',
    [switch]$RefreshConfig
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$config = Get-Content -LiteralPath $EimConfig -Raw | ConvertFrom-Json
$idf = $config.idfInstalled | Where-Object name -Match '^v6\.1(?:\.\d+)?$' | Sort-Object { [version]($_.name.Substring(1)) } -Descending | Select-Object -First 1
if (!$idf) { throw 'Install ESP-IDF v6.1 with EIM first.' }

$previous = @{}
try {
    $previous['PYTHONUTF8'] = [Environment]::GetEnvironmentVariable('PYTHONUTF8', 'Process')
    [Environment]::SetEnvironmentVariable('PYTHONUTF8', '1', 'Process')
    # ESP-SR model packer prints Unicode; Windows redirected output may use cp1252.
    $previous['PYTHONIOENCODING'] = [Environment]::GetEnvironmentVariable('PYTHONIOENCODING', 'Process')
    [Environment]::SetEnvironmentVariable('PYTHONIOENCODING', 'utf-8', 'Process')
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
    if ($LASTEXITCODE -ne 0 -or "$version" -notmatch 'ESP-IDF v6\.1(?:\.\d+)?(?:\s|$)') {
        throw "Expected ESP-IDF v6.1, got: $version"
    }
    Push-Location $root
    try {
        if ($RefreshConfig) {
            $sdkconfig = Join-Path $root 'sdkconfig'
            if (Test-Path -LiteralPath $sdkconfig) {
                $backups = Join-Path $root '.cache\sdkconfig-backups'
                New-Item -ItemType Directory -Force -Path $backups | Out-Null
                $backup = Join-Path $backups ('sdkconfig-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fffffff'))
                Move-Item -LiteralPath $sdkconfig -Destination $backup
                Write-Host "Previous sdkconfig saved to $backup"
            }
            # Opt-in refresh: regenerate defaults, never silently replace custom settings.
            & $idf.python $idfPy -B $BuildDirectory '-DIDF_TARGET=esp32s3' reconfigure
            if ($LASTEXITCODE -ne 0) { throw 'SDK configuration refresh failed; the previous config is in .cache/sdkconfig-backups.' }
        }
        & $idf.python $idfPy -B $BuildDirectory '-DIDF_TARGET=esp32s3' $Action
        if ($LASTEXITCODE -ne 0) { throw "ESP-IDF $Action failed ($LASTEXITCODE)." }
    } finally { Pop-Location }
} finally {
    foreach ($key in $previous.Keys) {
        [Environment]::SetEnvironmentVariable($key, $previous[$key], 'Process')
    }
}
