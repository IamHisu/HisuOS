$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$compiler = Get-ChildItem -Path 'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe' |
    Sort-Object FullName | Select-Object -Last 1
if (!$compiler) { throw 'MSVC x64 compiler is required.' }
$msvc = $compiler.Directory.Parent.Parent.Parent.FullName
$sdk = Get-ChildItem -Directory -LiteralPath 'C:\Program Files (x86)\Windows Kits\10\Include' |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt\stdio.h') } |
    Sort-Object Name | Select-Object -Last 1
if (!$sdk) { throw 'Windows SDK UCRT headers are required.' }
$sdkLib = Join-Path 'C:\Program Files (x86)\Windows Kits\10\Lib' $sdk.Name
$out = Join-Path $root 'build\network-tests'
$stubs = Join-Path $out 'stubs'
New-Item -ItemType Directory -Force -Path (Join-Path $stubs 'freertos') | Out-Null
# Generated include shims route hardware APIs to the deterministic host mock.
foreach ($header in @('esp_err.h', 'hisu_clock.h', 'esp_event.h', 'esp_netif.h', 'esp_timer.h',
    'esp_wifi.h', 'esp_random.h', 'esp_mac.h', 'esp_log.h', 'esp_http_client.h', 'esp_crt_bundle.h', 'freertos/FreeRTOS.h', 'freertos/task.h',
    'freertos/queue.h', 'freertos/semphr.h', 'freertos/event_groups.h')) {
    Set-Content -LiteralPath (Join-Path $stubs $header) -Value '#include "network_mock.h"' -Encoding ascii
}
& $compiler.FullName /nologo /c /std:c11 /MT /GS- /W3 /wd4101 /wd4505 "/I$msvc\include" "/I$($sdk.FullName)\ucrt" "/I$stubs" "/I$root\tests" "/I$root\components\hisu_network\include" "/I$root\components\hisu_settings\include" "/Fo$out\" (Join-Path $root 'tests\test_network.c') (Join-Path $root 'components\hisu_network\src\hisu_wifi_input.c')
if ($LASTEXITCODE -ne 0) { throw 'Network host compilation failed.' }
$exe = Join-Path $out 'network-test.exe'
& (Join-Path $compiler.DirectoryName 'link.exe') /nologo /subsystem:console "/out:$exe" "/LIBPATH:$msvc\lib\x64" "/LIBPATH:$sdkLib\ucrt\x64" "/LIBPATH:$sdkLib\um\x64" (Join-Path $out 'test_network.obj') (Join-Path $out 'hisu_wifi_input.obj')
if ($LASTEXITCODE -ne 0) { throw 'Network host linking failed.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Network host tests failed.' }
foreach ($header in @('nvs.h','nvs_flash.h')) {
    Set-Content -LiteralPath (Join-Path $stubs $header) -Value '#include "settings_mock.h"' -Encoding ascii
}
& $compiler.FullName /nologo /c /std:c11 /MT /GS- /W3 /wd4101 /wd4505 "/I$msvc\include" "/I$($sdk.FullName)\ucrt" "/I$stubs" "/I$root\tests" "/I$root\components\hisu_settings\include" "/Fo$out\" (Join-Path $root 'tests\test_wifi_store.c')
if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi store compilation failed.' }
$storeExe = Join-Path $out 'wifi-store-test.exe'
& (Join-Path $compiler.DirectoryName 'link.exe') /nologo /subsystem:console "/out:$storeExe" "/LIBPATH:$msvc\lib\x64" "/LIBPATH:$sdkLib\ucrt\x64" "/LIBPATH:$sdkLib\um\x64" (Join-Path $out 'test_wifi_store.obj')
if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi store linking failed.' }
& $storeExe
if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi store tests failed.' }
