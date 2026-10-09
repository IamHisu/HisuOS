param([string]$Clang = 'clang')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build\live-tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$wasm = Join-Path $out 'live-message.wasm'
& $Clang --target=wasm32 -nostdlib -O1 -Werror '-Wl,--no-entry' '-Wl,--export-all' "-I$root\components\hisu_voice\src" (Join-Path $root 'components\hisu_voice\src\hisu_voice_live_message.c') (Join-Path $root 'tests\test_live_message.c') -o $wasm
if ($LASTEXITCODE -ne 0) { throw 'Live framing compilation failed.' }
& node (Join-Path $root 'tests\test_live_message.mjs') $wasm
if ($LASTEXITCODE -ne 0) { throw 'Live framing tests failed.' }
