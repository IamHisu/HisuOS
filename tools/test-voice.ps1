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
& $compiler.FullName /nologo /c /std:c11 /MT /utf-8 /GS- /W4 /WX "/I$msvc\include" "/I$($sdk.FullName)\ucrt" "/I$root\managed_components\espressif__cjson\cJSON" "/I$root\components\hisu_voice\src" "/Fo$out\" (Join-Path $root 'tests\test_voice.c') (Join-Path $root 'components\hisu_voice\src\hisu_voice_session.c') (Join-Path $root 'components\hisu_voice\src\hisu_voice_sse.c') (Join-Path $root 'components\hisu_voice\src\hisu_voice_protocol.c')
if ($LASTEXITCODE -ne 0) { throw 'Voice test compilation failed.' }
& $compiler.FullName /nologo /c /std:c11 /MT /GS- /W3 "/I$msvc\include" "/I$($sdk.FullName)\ucrt" "/Fo$out\" (Join-Path $root 'managed_components\espressif__cjson\cJSON\cJSON.c')
if ($LASTEXITCODE -ne 0) { throw 'JSON test dependency compilation failed.' }
$exe = Join-Path $out 'voice-test.exe'
& (Join-Path $compiler.DirectoryName 'link.exe') /nologo /subsystem:console "/out:$exe" "/LIBPATH:$msvc\lib\x64" "/LIBPATH:$sdkLib\ucrt\x64" "/LIBPATH:$sdkLib\um\x64" (Join-Path $out 'test_voice.obj') (Join-Path $out 'hisu_voice_session.obj') (Join-Path $out 'hisu_voice_sse.obj') (Join-Path $out 'hisu_voice_protocol.obj') (Join-Path $out 'cJSON.obj')
if ($LASTEXITCODE -ne 0) { throw 'Voice test linking failed.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Voice tests failed.' }
