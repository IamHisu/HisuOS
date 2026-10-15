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
$out = Join-Path $root 'build\voice-tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
& $compiler.FullName /nologo /c /std:c11 /MT /utf-8 /GS- /W4 /WX "/I$msvc\include" "/I$($sdk.FullName)\ucrt" "/I$root\managed_components\espressif__cjson\cJSON" "/I$root\components\hisu_voice\src" "/Fo$out\" (Join-Path $root 'tests\test_voice.c') (Join-Path $root 'components\hisu_voice\src\hisu_voice_session.c') (Join-Path $root 'components\hisu_voice\src\hisu_voice_sse.c') (Join-Path $root 'components\hisu_voice\src\hisu_voice_protocol.c') (Join-Path $root 'components\hisu_voice\src\hisu_voice_live_turn.c')
if ($LASTEXITCODE -ne 0) { throw 'Voice test compilation failed.' }
& $compiler.FullName /nologo /c /std:c11 /MT /GS- /W3 "/I$msvc\include" "/I$($sdk.FullName)\ucrt" "/Fo$out\" (Join-Path $root 'managed_components\espressif__cjson\cJSON\cJSON.c')
if ($LASTEXITCODE -ne 0) { throw 'JSON test dependency compilation failed.' }
$exe = Join-Path $out 'voice-test.exe'
& (Join-Path $compiler.DirectoryName 'link.exe') /nologo /subsystem:console "/out:$exe" "/LIBPATH:$msvc\lib\x64" "/LIBPATH:$sdkLib\ucrt\x64" "/LIBPATH:$sdkLib\um\x64" (Join-Path $out 'test_voice.obj') (Join-Path $out 'hisu_voice_session.obj') (Join-Path $out 'hisu_voice_sse.obj') (Join-Path $out 'hisu_voice_protocol.obj') (Join-Path $out 'hisu_voice_live_turn.obj') (Join-Path $out 'cJSON.obj')
if ($LASTEXITCODE -ne 0) { throw 'Voice test linking failed.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Voice tests failed.' }
$stubs = Join-Path $out 'stubs'
foreach ($directory in @('', 'driver', 'freertos', 'mbedtls')) {
    New-Item -ItemType Directory -Force -Path (Join-Path $stubs $directory) | Out-Null
}
# Generated include shims replace SDK hardware APIs for the actual Live transport source.
foreach ($header in @('esp_err.h', 'esp_app_desc.h', 'esp_system.h', 'esp_websocket_client.h', 'esp_crt_bundle.h', 'esp_heap_caps.h',
    'esp_timer.h', 'esp_log.h', 'driver/spi_master.h', 'driver/i2s_std.h', 'freertos/FreeRTOS.h',
    'freertos/task.h', 'freertos/queue.h', 'mbedtls/base64.h')) {
    Set-Content -LiteralPath (Join-Path $stubs $header) -Value '#include "live_transport_mock.h"' -Encoding ascii
}
& $compiler.FullName /nologo /c /std:c11 /MT /utf-8 /GS- /W3 /wd4505 "/I$msvc\include" "/I$($sdk.FullName)\ucrt" "/I$stubs" "/I$root\tests" "/I$root\components\hisu_media\include" "/I$root\components\hisu_network\include" "/I$root\components\hisu_settings\include" "/I$root\components\hisu_voice\src" "/I$root\boards\zhengchen_154" "/I$root\managed_components\espressif__cjson\cJSON" "/Fo$out\" (Join-Path $root 'tests\test_live_transport.c') (Join-Path $root 'components\hisu_voice\src\hisu_voice_live_message.c')
if ($LASTEXITCODE -ne 0) { throw 'Live transport host compilation failed.' }
$liveExe = Join-Path $out 'live-transport-test.exe'
& (Join-Path $compiler.DirectoryName 'link.exe') /nologo /subsystem:console "/out:$liveExe" "/LIBPATH:$msvc\lib\x64" "/LIBPATH:$sdkLib\ucrt\x64" "/LIBPATH:$sdkLib\um\x64" (Join-Path $out 'test_live_transport.obj') (Join-Path $out 'hisu_voice_live_message.obj') (Join-Path $out 'hisu_voice_live_turn.obj') (Join-Path $out 'cJSON.obj')
if ($LASTEXITCODE -ne 0) { throw 'Live transport host linking failed.' }
& $liveExe
if ($LASTEXITCODE -ne 0) { throw 'Live transport host tests failed.' }
