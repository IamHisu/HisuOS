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
$out = Join-Path $root 'build\alert-tests'
$stubs = Join-Path $out 'stubs'
foreach ($dir in @('','freertos','mbedtls')) { New-Item -ItemType Directory -Force -Path (Join-Path $stubs $dir) | Out-Null }
foreach ($header in @('esp_err.h','esp_partition.h','esp_heap_caps.h','esp_http_client.h','esp_crt_bundle.h',
    'esp_log.h','esp_timer.h','strings.h','mbedtls/base64.h','freertos/FreeRTOS.h','freertos/semphr.h','freertos/task.h')) {
    Set-Content -LiteralPath (Join-Path $stubs $header) -Value '#include "alert_mock.h"' -Encoding ascii
}
& $compiler.FullName /nologo /c /std:c11 /MT /utf-8 /GS- /W3 /wd4101 /wd4505 "/I$msvc\include" "/I$($sdk.FullName)\ucrt" "/I$stubs" "/I$root\tests" "/I$root\components\hisu_alert\include" "/I$root\components\hisu_memory\include" "/I$root\components\hisu_power\include" "/I$root\components\hisu_network\include" "/I$root\components\hisu_settings\include" "/I$root\managed_components\espressif__cjson\cJSON" "/Fo$out\" (Join-Path $root 'tests\test_alert.c') (Join-Path $root 'components\hisu_alert\src\hisu_alert_model.c') (Join-Path $root 'managed_components\espressif__cjson\cJSON\cJSON.c')
if ($LASTEXITCODE -ne 0) { throw 'Alert test compilation failed.' }
$exe = Join-Path $out 'alert-test.exe'
& (Join-Path $compiler.DirectoryName 'link.exe') /nologo /subsystem:console "/out:$exe" "/LIBPATH:$msvc\lib\x64" "/LIBPATH:$sdkLib\ucrt\x64" "/LIBPATH:$sdkLib\um\x64" (Join-Path $out 'test_alert.obj') (Join-Path $out 'hisu_alert_model.obj') (Join-Path $out 'cJSON.obj')
if ($LASTEXITCODE -ne 0) { throw 'Alert test linking failed.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Alert tests failed.' }
& $compiler.FullName /nologo /c /std:c11 /MT /utf-8 /GS- /W3 /wd4101 /wd4505 "/I$msvc\include" "/I$($sdk.FullName)\ucrt" "/I$stubs" "/I$root\tests" "/I$root\components\hisu_memory\include" "/I$root\components\hisu_power\include" "/I$root\components\hisu_network\include" "/I$root\components\hisu_settings\include" "/I$root\managed_components\espressif__cjson\cJSON" "/Fo$out\" (Join-Path $root 'tests\test_memory_summary.c')
if ($LASTEXITCODE -ne 0) { throw 'Memory summary HTTP compilation failed.' }
$summaryExe = Join-Path $out 'memory-summary-test.exe'
& (Join-Path $compiler.DirectoryName 'link.exe') /nologo /subsystem:console "/out:$summaryExe" "/LIBPATH:$msvc\lib\x64" "/LIBPATH:$sdkLib\ucrt\x64" "/LIBPATH:$sdkLib\um\x64" (Join-Path $out 'test_memory_summary.obj') (Join-Path $out 'cJSON.obj')
if ($LASTEXITCODE -ne 0) { throw 'Memory summary HTTP linking failed.' }
& $summaryExe
if ($LASTEXITCODE -ne 0) { throw 'Memory summary HTTP tests failed.' }
