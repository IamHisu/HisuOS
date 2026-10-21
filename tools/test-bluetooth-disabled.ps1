$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$defaults = Get-Content -LiteralPath (Join-Path $root 'sdkconfig.defaults') -Raw
if ($defaults -notmatch '(?m)^# CONFIG_BT_ENABLED is not set\r?$' -or
    $defaults -match '(?m)^CONFIG_BT_.*=y\r?$') {
    throw 'Bluetooth must remain disabled in sdkconfig.defaults.'
}
foreach ($path in @('components/hisu_ble/CMakeLists.txt', 'components/hisu_ble/src/hisu_ble.c',
    'components/hisu_ble/include/hisu_ble.h', 'docs/ble/app.js', '.github/workflows/ble-pages.yml')) {
    if (Test-Path -LiteralPath (Join-Path $root $path)) { throw "Removed Bluetooth artifact still exists: $path" }
}
$cmake = Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw
if ($cmake -notmatch 'if\(CONFIG_BT_ENABLED\)') { throw 'Missing stale-sdkconfig Bluetooth guard.' }
$configPath = Join-Path $root 'sdkconfig'
if ((Test-Path -LiteralPath $configPath) -and
    (Select-String -LiteralPath $configPath -Pattern '^CONFIG_BT_.*=y$' -Quiet)) {
    throw 'Existing sdkconfig still enables Bluetooth. Disable it or refresh configuration.'
}
Write-Host 'Bluetooth removal checks passed: disabled defaults/config, removed artifacts and stale-config guard.'
