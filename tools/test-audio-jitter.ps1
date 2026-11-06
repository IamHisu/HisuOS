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
$out = Join-Path $root 'build\audio-jitter-tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
& $compiler.FullName /nologo /c /std:c11 /MT /W4 /WX "/I$msvc\include" "/I$($sdk.FullName)\ucrt" "/I$root\components\hisu_voice\src" "/Fo$out\" (Join-Path $root 'tests\test_audio_jitter.c')
if ($LASTEXITCODE -ne 0) { throw 'Audio jitter compilation failed.' }
$exe = Join-Path $out 'audio-jitter-test.exe'
& (Join-Path $compiler.DirectoryName 'link.exe') /nologo /subsystem:console "/out:$exe" "/LIBPATH:$msvc\lib\x64" "/LIBPATH:$sdkLib\ucrt\x64" "/LIBPATH:$sdkLib\um\x64" (Join-Path $out 'test_audio_jitter.obj')
if ($LASTEXITCODE -ne 0) { throw 'Audio jitter linking failed.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Audio jitter tests failed.' }
