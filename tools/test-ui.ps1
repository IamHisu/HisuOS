param([string]$EimConfig = 'C:\Espressif\tools\eim_idf.json')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$env:INCLUDE = "$root\components\hisu_memory\include;$env:INCLUDE"
$config = Get-Content -LiteralPath $EimConfig -Raw | ConvertFrom-Json
$idf = $config.idfInstalled | Where-Object name -eq 'v5.5.5' | Select-Object -First 1
if (!$idf) { throw 'ESP-IDF v5.5.5 EIM installation required.' }
& $idf.python (Join-Path $root 'tests\test_wallpaper_policy.py')
if ($LASTEXITCODE -ne 0) { throw 'Wallpaper policy checks failed.' }
& $idf.python (Join-Path $root 'tests\test_ram_diagnostics.py')
if ($LASTEXITCODE -ne 0) { throw 'RAM diagnostic contracts failed.' }
$compiler = Get-ChildItem -Path 'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe' |
    Sort-Object FullName | Select-Object -Last 1
if (!$compiler) { throw 'MSVC x64 compiler is required for host tests.' }
$linker = Join-Path $compiler.DirectoryName 'link.exe'
$msvc = $compiler.Directory.Parent.Parent.Parent.FullName
$include = Join-Path $msvc 'include'
$sdk = Get-ChildItem -Directory -LiteralPath 'C:\Program Files (x86)\Windows Kits\10\Include' |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt\math.h') } |
    Sort-Object Name | Select-Object -Last 1
if (!$sdk) { throw 'Windows SDK UCRT headers are required for calendar tests.' }
$sdkLib = Join-Path 'C:\Program Files (x86)\Windows Kits\10\Lib' $sdk.Name
$out = Join-Path $root 'build\ui-tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$object = Join-Path $out 'model.obj'
$wifiObject = Join-Path $out 'wifi-input.obj'
$powerObject = Join-Path $out 'power-model.obj'
$dll = Join-Path $out 'model.dll'
& $compiler.FullName /nologo /c /std:c11 /O1 /GS- /Zl /W4 /WX "/I$include" "/Fo$object" (Join-Path $root 'components\hisu_ui\src\hisu_ui_model.c')
if ($LASTEXITCODE -ne 0) { throw 'Host model compilation failed.' }
& $compiler.FullName /nologo /c /std:c11 /O1 /GS- /Zl /W4 /WX "/I$include" "/Fo$wifiObject" (Join-Path $root 'components\hisu_network\src\hisu_wifi_input.c')
if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi input compilation failed.' }
& $compiler.FullName /nologo /c /std:c11 /O1 /GS- /Zl /W4 /WX "/I$include" "/Fo$powerObject" (Join-Path $root 'components\hisu_power\src\hisu_power_model.c')
if ($LASTEXITCODE -ne 0) { throw 'Power model compilation failed.' }
& $linker /nologo /dll /noentry /nodefaultlib /machine:x64 "/out:$dll" /export:hisu_ui_model_key /export:hisu_ui_adjust /export:hisu_ui_battery_bars /export:hisu_ui_battery_color /export:hisu_ui_battery_pulse /export:hisu_ui_battery_charge_bar /export:hisu_ui_valid_utf8 /export:hisu_ui_overlay_dark_ink /export:hisu_wifi_password_is_valid /export:hisu_wifi_can_auto_join /export:hisu_wifi_setup_password /export:hisu_power_percent_from_adc /export:hisu_power_remaining_seconds $object $wifiObject $powerObject
if ($LASTEXITCODE -ne 0) { throw 'Host model linking failed.' }
& $idf.python (Join-Path $root 'tests\test_ui_model.py') $dll
if ($LASTEXITCODE -ne 0) { throw 'UI model tests failed.' }
$timing = Join-Path $out 'ui-timing-test.exe'
& $compiler.FullName /nologo /c /std:c11 /MT /GS- /W4 /WX "/I$include" "/I$($sdk.FullName)\ucrt" "/I$root\components\hisu_ui\src" "/Fo$out\" (Join-Path $root 'tests\test_ui_timing.c')
if ($LASTEXITCODE -ne 0) { throw 'UI timing compilation failed.' }
& $linker /nologo /subsystem:console "/out:$timing" "/LIBPATH:$msvc\lib\x64" "/LIBPATH:$sdkLib\ucrt\x64" "/LIBPATH:$sdkLib\um\x64" (Join-Path $out 'test_ui_timing.obj')
if ($LASTEXITCODE -ne 0) { throw 'UI timing linking failed.' }
& $timing
if ($LASTEXITCODE -ne 0) { throw 'UI timing tests failed.' }
$face = Join-Path $out 'ui-face-test.exe'
& $compiler.FullName /nologo /c /std:c11 /MT /GS- /W4 /WX "/I$include" "/I$($sdk.FullName)\ucrt" "/I$root\components\hisu_ui\include" "/Fo$out\" (Join-Path $root 'tests\test_ui_face.c')
if ($LASTEXITCODE -ne 0) { throw 'UI face compilation failed.' }
& $linker /nologo /subsystem:console "/out:$face" "/LIBPATH:$msvc\lib\x64" "/LIBPATH:$sdkLib\ucrt\x64" "/LIBPATH:$sdkLib\um\x64" (Join-Path $out 'test_ui_face.obj')
if ($LASTEXITCODE -ne 0) { throw 'UI face linking failed.' }
& $face
if ($LASTEXITCODE -ne 0) { throw 'UI face tests failed.' }
$calendar = Join-Path $out 'calendar-test.exe'
$calendarObject = Join-Path $out 'hisu_calendar.obj'
$calendarTestObject = Join-Path $out 'test_calendar.obj'
& $compiler.FullName /nologo /c /std:c11 /MT /GS- /W4 /WX "/I$include" "/I$($sdk.FullName)\ucrt" "/I$root\components\hisu_clock\include" "/Fo$out\" (Join-Path $root 'tests\test_calendar.c') (Join-Path $root 'components\hisu_clock\src\hisu_calendar.c')
if ($LASTEXITCODE -ne 0) { throw 'Calendar compilation failed.' }
& $linker /nologo /subsystem:console "/out:$calendar" "/LIBPATH:$msvc\lib\x64" "/LIBPATH:$sdkLib\ucrt\x64" "/LIBPATH:$sdkLib\um\x64" $calendarObject $calendarTestObject
if ($LASTEXITCODE -ne 0) { throw 'Calendar linking failed.' }
& $calendar
if ($LASTEXITCODE -ne 0) { throw 'Calendar tests failed.' }
$media = Join-Path $out 'media-test.exe'
& $compiler.FullName /nologo /c /std:c11 /MT /GS- /W4 /WX "/I$include" "/I$($sdk.FullName)\ucrt" "/I$root\components\hisu_media\src" "/Fo$out\" (Join-Path $root 'tests\test_media_model.c') (Join-Path $root 'components\hisu_media\src\hisu_media_model.c')
if ($LASTEXITCODE -ne 0) { throw 'Media model compilation failed.' }
& $linker /nologo /subsystem:console "/out:$media" "/LIBPATH:$msvc\lib\x64" "/LIBPATH:$sdkLib\ucrt\x64" "/LIBPATH:$sdkLib\um\x64" (Join-Path $out 'test_media_model.obj') (Join-Path $out 'hisu_media_model.obj')
if ($LASTEXITCODE -ne 0) { throw 'Media model linking failed.' }
& $media
if ($LASTEXITCODE -ne 0) { throw 'Media model tests failed.' }
$dns = Join-Path $out 'setup-dns-test.exe'
& $compiler.FullName /nologo /c /std:c11 /MT /GS- /W4 /WX "/I$include" "/I$($sdk.FullName)\ucrt" "/I$root\components\hisu_media\src" "/Fo$out\" (Join-Path $root 'tests\test_setup_dns.c') (Join-Path $root 'components\hisu_media\src\hisu_setup_dns.c')
if ($LASTEXITCODE -ne 0) { throw 'Setup DNS compilation failed.' }
& $linker /nologo /subsystem:console "/out:$dns" "/LIBPATH:$msvc\lib\x64" "/LIBPATH:$sdkLib\ucrt\x64" "/LIBPATH:$sdkLib\um\x64" (Join-Path $out 'test_setup_dns.obj') (Join-Path $out 'hisu_setup_dns.obj')
if ($LASTEXITCODE -ne 0) { throw 'Setup DNS linking failed.' }
& $dns
if ($LASTEXITCODE -ne 0) { throw 'Setup DNS tests failed.' }
