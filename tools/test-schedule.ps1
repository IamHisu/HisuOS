$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$compiler = Get-ChildItem -Path 'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe' |
    Sort-Object FullName | Select-Object -Last 1
if (!$compiler) { throw 'MSVC x64 compiler is required.' }
$msvc = $compiler.Directory.Parent.Parent.Parent.FullName
$sdk = Get-ChildItem -Directory -LiteralPath 'C:\Program Files (x86)\Windows Kits\10\Include' |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt\stdio.h') } |
    Sort-Object Name | Select-Object -Last 1
$sdkLib = Join-Path 'C:\Program Files (x86)\Windows Kits\10\Lib' $sdk.Name
$out = Join-Path $root 'build\schedule-tests'
$stubs = Join-Path $out 'stubs'
New-Item -ItemType Directory -Force -Path (Join-Path $stubs 'freertos') | Out-Null
foreach ($header in @('esp_err.h','esp_partition.h','esp_heap_caps.h','freertos/FreeRTOS.h','freertos/semphr.h')) {
    Set-Content -LiteralPath (Join-Path $stubs $header) -Value '#include "schedule_mock.h"' -Encoding ascii
}
& $compiler.FullName /nologo /c /std:c11 /MT /utf-8 /GS- /W3 /wd4101 /wd4505 "/I$msvc\include" "/I$($sdk.FullName)\ucrt" "/I$stubs" "/I$root\tests" "/I$root\components\hisu_schedule\include" "/I$root\components\hisu_memory\include" "/I$root\managed_components\espressif__cjson\cJSON" "/Fo$out\" (Join-Path $root 'tests\test_schedule.c') (Join-Path $root 'components\hisu_memory\src\hisu_memory_model.c') (Join-Path $root 'managed_components\espressif__cjson\cJSON\cJSON.c')
if ($LASTEXITCODE -ne 0) { throw 'Schedule test compilation failed.' }
$exe = Join-Path $out 'schedule-test.exe'
& (Join-Path $compiler.DirectoryName 'link.exe') /nologo /subsystem:console "/out:$exe" "/LIBPATH:$msvc\lib\x64" "/LIBPATH:$sdkLib\ucrt\x64" "/LIBPATH:$sdkLib\um\x64" (Join-Path $out 'test_schedule.obj') (Join-Path $out 'hisu_memory_model.obj') (Join-Path $out 'cJSON.obj')
if ($LASTEXITCODE -ne 0) { throw 'Schedule test linking failed.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Schedule tests failed.' }
