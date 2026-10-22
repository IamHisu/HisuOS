$ErrorActionPreference = 'Stop'
$env:INCLUDE = "$PSScriptRoot\..\components\hisu_ui\include;$env:INCLUDE"
$root = Split-Path -Parent $PSScriptRoot
$env:INCLUDE = "$root\components\hisu_schedule\include;$env:INCLUDE"
$env:INCLUDE = "$root\components\hisu_memory\include;$env:INCLUDE"
$env:INCLUDE = "$root\components\hisu_alert\include;$root\components\hisu_power\include;$env:INCLUDE"
foreach ($name in @('sdkconfig.defaults', 'sdkconfig')) {
    $path = Join-Path $root $name
    if (!(Test-Path -LiteralPath $path)) { continue }
    $settings = Get-Content -LiteralPath $path
    if (!($settings -contains 'CONFIG_LWIP_TCP_SND_BUF_DEFAULT=16384')) {
        throw "$name must retain the 16 KiB TCP send budget for the voice uplink."
    }
    if (!($settings -contains 'CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y') -or
        !(($settings -contains 'CONFIG_MBEDTLS_HARDWARE_AES=n') -or
          ($settings -contains '# CONFIG_MBEDTLS_HARDWARE_AES is not set')) -or
        ($settings -contains 'CONFIG_MBEDTLS_HARDWARE_AES=y')) {
        throw "$name must keep TLS in PSRAM and disable AES DMA acceleration for audio memory headroom."
    }
}
Write-Output 'TLS/audio configuration checks passed (PSRAM TLS, software AES).'
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
$cryptoStubs = Join-Path $out 'crypto-stubs'
New-Item -ItemType Directory -Force -Path $cryptoStubs | Out-Null
foreach ($header in @('includes.h','common.h','crypto.h')) {
    Set-Content -LiteralPath (Join-Path $cryptoStubs $header) -Encoding ascii -Value "#pragma once`n#define CRYPTO_H`n#include <stdint.h>`n#include <stddef.h>`ntypedef uint8_t u8;`ntypedef uint32_t u32;"
}
$idf = if ($env:IDF_PATH) { $env:IDF_PATH } else { 'C:\esp\v5.5.5\esp-idf' }
& $compiler.FullName /nologo /c /std:c11 /MT /GS- /W3 "/I$msvc\include" "/I$($sdk.FullName)\ucrt" "/I$cryptoStubs" "/Fo$out\rc4.obj" (Join-Path $idf 'components\wpa_supplicant\src\crypto\rc4.c')
if ($LASTEXITCODE -ne 0) { throw 'SDK lyric crypto dependency compilation failed.' }
foreach ($directory in @('', 'driver', 'freertos', 'mbedtls', 'lwip')) {
    New-Item -ItemType Directory -Force -Path (Join-Path $stubs $directory) | Out-Null
}
# Generated include shims replace SDK hardware APIs for the actual Live transport source.
foreach ($header in @('esp_err.h', 'esp_app_desc.h', 'esp_system.h', 'esp_websocket_client.h', 'esp_crt_bundle.h', 'esp_heap_caps.h',
    'esp_transport_ssl.h', 'esp_transport_ws.h', 'lwip/sockets.h', 'lwip/tcp.h',
    'esp_timer.h', 'esp_log.h', 'driver/spi_master.h', 'driver/i2s_std.h', 'freertos/FreeRTOS.h',
    'freertos/task.h', 'freertos/queue.h', 'freertos/idf_additions.h', 'mbedtls/base64.h')) {
    Set-Content -LiteralPath (Join-Path $stubs $header) -Value '#include "live_transport_mock.h"' -Encoding ascii
}
& $compiler.FullName /nologo /c /std:c11 /MT /utf-8 /GS- /W3 /wd4505 "/I$msvc\include" "/I$($sdk.FullName)\ucrt" "/I$stubs" "/I$root\tests" "/I$root\components\hisu_media\include" "/I$root\components\hisu_network\include" "/I$root\components\hisu_settings\include" "/I$root\components\hisu_voice\src" "/I$root\boards\zhengchen_154" "/I$root\managed_components\espressif__cjson\cJSON" "/Fo$out\" (Join-Path $root 'tests\test_live_transport.c') (Join-Path $root 'components\hisu_voice\src\hisu_voice_live_message.c') (Join-Path $root 'components\hisu_voice\src\hisu_voice_music_protocol.c')
if ($LASTEXITCODE -ne 0) { throw 'Live transport host compilation failed.' }
$liveExe = Join-Path $out 'live-transport-test.exe'
& (Join-Path $compiler.DirectoryName 'link.exe') /nologo /subsystem:console "/out:$liveExe" "/LIBPATH:$msvc\lib\x64" "/LIBPATH:$sdkLib\ucrt\x64" "/LIBPATH:$sdkLib\um\x64" (Join-Path $out 'test_live_transport.obj') (Join-Path $out 'hisu_voice_live_message.obj') (Join-Path $out 'hisu_voice_music_protocol.obj') (Join-Path $out 'hisu_voice_live_turn.obj') (Join-Path $out 'cJSON.obj') (Join-Path $out 'rc4.obj')
if ($LASTEXITCODE -ne 0) { throw 'Live transport host linking failed.' }
& $liveExe
if ($LASTEXITCODE -ne 0) { throw 'Live transport host tests failed.' }
$musicStubs = Join-Path $out 'music-stubs'
foreach ($directory in @('', 'freertos', 'mbedtls')) {
    New-Item -ItemType Directory -Force -Path (Join-Path $musicStubs $directory) | Out-Null
}
foreach ($header in @('esp_err.h','esp_http_client.h','esp_crt_bundle.h','esp_heap_caps.h',
    'esp_audio_simple_dec.h','esp_mp3_dec.h','esp_log.h','mbedtls/md.h','freertos/FreeRTOS.h','strings.h','miniz.h')) {
    Set-Content -LiteralPath (Join-Path $musicStubs $header) -Value '#include "music_mock.h"' -Encoding ascii
}
& $compiler.FullName /nologo /c /std:c11 /MT /utf-8 /GS- /W3 /wd4505 "/I$msvc\include" "/I$($sdk.FullName)\ucrt" "/I$musicStubs" "/I$root\tests" "/I$root\components\hisu_voice\src" "/I$root\managed_components\espressif__cjson\cJSON" "/Fo$out\" (Join-Path $root 'tests\test_music.c') (Join-Path $root 'components\hisu_voice\src\hisu_voice_music_protocol.c')
if ($LASTEXITCODE -ne 0) { throw 'Music host compilation failed.' }
$musicExe = Join-Path $out 'music-test.exe'
& (Join-Path $compiler.DirectoryName 'link.exe') /nologo /subsystem:console "/out:$musicExe" "/LIBPATH:$msvc\lib\x64" "/LIBPATH:$sdkLib\ucrt\x64" "/LIBPATH:$sdkLib\um\x64" (Join-Path $out 'test_music.obj') (Join-Path $out 'hisu_voice_music_protocol.obj') (Join-Path $out 'cJSON.obj') (Join-Path $out 'rc4.obj')
if ($LASTEXITCODE -ne 0) { throw 'Music host linking failed.' }
& $musicExe
if ($LASTEXITCODE -ne 0) { throw 'Music host tests failed.' }
