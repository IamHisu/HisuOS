# ESP-IDF 6.1

HisuOS targets ESP32-S3 with ESP-IDF 6.1.x. Install that version with EIM, then
run from the repository root:

```powershell
.\tools\build.ps1 -RefreshConfig
```

Use `-RefreshConfig` once when moving from 5.5.5. It backs up the existing
`sdkconfig` under `.cache/sdkconfig-backups/` and regenerates it from
`sdkconfig.defaults`. Review any custom settings before flashing. Subsequent
builds use `.\tools\build.ps1` without resetting configuration.

The default output directory is `build-idf61/`, separate from the old `build/`.
Firmware is `build-idf61/HisuOS.bin`. With the 6.1 environment activated, manual
commands must select the same directory, e.g. `idf.py -B build-idf61 size` or
`idf.py -B build-idf61 flash`. The helper does not flash or erase the device.

If plain `idf.py build` fails with a bootloader CMake error mentioning both
`v6.1/.../bootloader/subproject` and `v5.5.5/.../bootloader/subproject`, the
default `build/bootloader/` still contains a 5.5.5 CMake cache. Reconfiguring
the app alone does not replace that nested cache. Use the separate, verified
directory:

```powershell
idf.py -B build-idf61 build
```

To keep using the default `build/` directory instead, archive its `bootloader/`
and `bootloader-prefix/` directories first, then run
`.\tools\build.ps1 -Action reconfigure -BuildDirectory build` followed by
`.\tools\build.ps1 -BuildDirectory build`. Retain the root `sdkconfig` and
local audio assets. This repairs generated build files without erasing NVS or
flashing the board. Choose the same build directory for build and flash.

## Changes

- Manifests require IDF 6.1.x. The lock file records the resolved SDK and libraries.
- The removed built-in `json` dependency is replaced by explicit
  `espressif/cjson` dependencies for media, memory, schedules and voice. cJSON
  stays at 1.7.19~2 because the lookup parser includes a private namespaced copy
  whose declarations must match the upstream header.
- ESP-SR 2.5.5, ESP-DL 3.3.13, audio codec 2.6.2 and LVGL 9.4.0 are retained.
  This avoids mixing an AI/library upgrade into the SDK migration.
- Build, UI-test, voice-test and USB-chat helpers select the 6.1 installation.
- Bootloader GPIO routing uses `gpio_ll_set_output_signal_matrix_source` and
  the `esp_hal_gpio` component instead of the removed
  `gpio_ll_matrix_out_default` helper. Backlight/reset levels remain low.
- The PSRAM/TLS allocation policy, software AES, 16 KiB TCP send buffer,
  Bluetooth exclusion and GPIO20 console restrictions remain enforced.

ESP-IDF 6.x changes the default C library to Picolibc and uses Mbed TLS 4 / PSA
Crypto. A successful build does not establish the runtime behavior of prebuilt
speech/audio libraries. Test wake recognition, microphone/speaker, repeated
Gemini connections, music playback, the Setting hotspot and display startup on
the board before relying on the migrated firmware.

Optional offline audio images under `.cache/battery-audio/` and
`.cache/feedback-audio/` are local artifacts. If missing, CMake warns and omits
them from the flash bundle. Building does not regenerate them or call Gemini.

## Verification

The ESP32-S3 firmware and bootloader build with ESP-IDF 6.1.0 / GCC 15.2.0.
`HisuOS.bin` is 3,077,888 bytes (`0x2ef700`), leaving 27% of the 4 MiB app slot.
The 31 USB-chat tests, three RAM diagnostic contracts, four wallpaper contracts
and four web/overlay/boot-GIF test files pass. The boot-GIF check uses the
bundled Sharp package through NODE_PATH. MSVC-based C host suites are not run on
this machine because MSVC is not installed. No device flash or hardware test
is included in these results.

References:

- [Migration from 5.5 to 6.0](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/migration-guides/release-6.x/6.0/index.html)
- [Migration from 6.0 to 6.1](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/migration-guides/release-6.x/6.1/index.html)

## TLS certificate chains

The firmware uses the full ESP-IDF certificate bundle and enables
`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY` for cross-signed
server chains. This preserves certificate and hostname verification.
The active `sdkconfig` must also enable this option; defaults alone do not
replace an existing disabled setting.

`No matching trusted root certificate found` means the presented chain could
not be matched to a trusted root. Cross-signed support may resolve this, but
an untrusted network interception certificate or a missing CA still needs
separate diagnosis. Internet probe failures now include the endpoint URL.
