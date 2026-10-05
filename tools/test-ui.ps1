param([string]$EimConfig = 'C:\Espressif\tools\eim_idf.json')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$config = Get-Content -LiteralPath $EimConfig -Raw | ConvertFrom-Json
$idf = $config.idfInstalled | Where-Object name -eq 'v5.5.5' | Select-Object -First 1
if (!$idf) { throw 'ESP-IDF v5.5.5 EIM installation required.' }
$compiler = Get-ChildItem -Path 'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe' |
    Sort-Object FullName | Select-Object -Last 1
if (!$compiler) { throw 'MSVC x64 compiler is required for host tests.' }
$linker = Join-Path $compiler.DirectoryName 'link.exe'
$out = Join-Path $root 'build\ui-tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$object = Join-Path $out 'model.obj'
$dll = Join-Path $out 'model.dll'
& $compiler.FullName /nologo /c /std:c11 /O1 /GS- /Zl /W4 /WX "/Fo$object" (Join-Path $root 'components\hisu_ui\src\hisu_ui_model.c')
if ($LASTEXITCODE -ne 0) { throw 'Host model compilation failed.' }
& $linker /nologo /dll /noentry /nodefaultlib /machine:x64 "/out:$dll" /export:hisu_ui_model_key /export:hisu_ui_adjust /export:hisu_ui_valid_utf8 $object
if ($LASTEXITCODE -ne 0) { throw 'Host model linking failed.' }
& $idf.python (Join-Path $root 'tests\test_ui_model.py') $dll
if ($LASTEXITCODE -ne 0) { throw 'UI model tests failed.' }
