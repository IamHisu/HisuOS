param([string]$Port = '', [string]$Python = '', [switch]$Test)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (!$Python) {
    $candidates = @()
    if ($env:IDF_PYTHON_ENV_PATH) { $candidates += Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe' }
    if ($env:VIRTUAL_ENV) { $candidates += Join-Path $env:VIRTUAL_ENV 'Scripts\python.exe' }
    $eimPath = 'C:\Espressif\tools\eim_idf.json'
    if (Test-Path -LiteralPath $eimPath) {
        $eim = Get-Content -LiteralPath $eimPath -Raw | ConvertFrom-Json
        $candidates += @($eim.idfInstalled | Where-Object name -eq 'v5.5.5' | ForEach-Object python)
    }
    $command = Get-Command python -ErrorAction SilentlyContinue
    if ($command) { $candidates += $command.Source }
    foreach ($candidate in $candidates | Select-Object -Unique) {
        if (!$candidate -or !(Test-Path -LiteralPath $candidate)) { continue }
        & $candidate -c 'import serial, tkinter' 2>$null
        if ($LASTEXITCODE -eq 0) { $Python = $candidate; break }
    }
}
if (!$Python) { throw 'Can Python co tkinter va pyserial. Hay dung Python cua ESP-IDF.' }
if ($Test) {
    Push-Location $root
    try {
        & $Python -m unittest discover -s tests -p test_chat.py -v
        if ($LASTEXITCODE -ne 0) { throw 'Chat tests failed.' }
    } finally { Pop-Location }
    return
}
if (!$Port) {
    $settings = Join-Path $root '.vscode\settings.json'
    if (Test-Path -LiteralPath $settings) {
        try { $Port = (Get-Content -LiteralPath $settings -Raw | ConvertFrom-Json).'idf.port' }
        catch { $Port = '' }
    }
}
$logs = Join-Path $root '.cache\chat-logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$arguments = @('"' + (Join-Path $PSScriptRoot 'hisu_chat.py') + '"')
if ($Port) {
    if ($Port -notmatch '^COM[0-9]+$') { throw 'Port must be a Windows COM port, for example COM8.' }
    $arguments += @('--port', $Port)
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fffffff'
$errorLog = Join-Path $logs "launcher-$stamp-error.log"
$process = Start-Process -FilePath $Python -ArgumentList $arguments -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $logs "launcher-$stamp-output.log") `
    -RedirectStandardError $errorLog
if ($process.WaitForExit(300) -and $process.ExitCode -ne 0) { throw "Chat Hi Su startup failed: $errorLog" }
Write-Host 'Da mo Chat Hi Su. Dong Monitor truoc khi bam Ket noi.'
