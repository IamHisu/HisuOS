# Validation

## Current baseline: ESP-IDF 5.5.5

Downgrade verification on 2026-10-05:

| Check | Result |
| --- | --- |
| `tools/build.ps1` / clean `idf.py build` | PASS, ESP-IDF 5.5.5, GCC 14.2.0, ESP32-S3 |
| Dependency resolution | PASS, lockfile records IDF 5.5.5; standalone cJSON removed |
| Firmware partition check | PASS, `build/HisuOS.bin`, 0x167190 bytes; app slot 0x1a9000; 16% free |
| Wrong-version guard | PASS, explicitly activated 6.0.3 rejected before build |

The official `v5.5.5` source and submodules were restored under
`.cache/esp-idf-v5.5.5`. Existing compiler/Python packages were reused. The
helper normalizes EIM's tools directory to the parent expected by the official
export script; the existing 5.5 constraints file was copied to that standard
tools root. The globally selected EIM 6.0.3 installation was not replaced.

The earlier generated build and sdkconfig are retained in `build-idf6.0.3`
and `sdkconfig.idf6.0.3`. The current `build` and `sdkconfig` were freshly
generated for 5.5.5. JSON now uses IDF's built-in `json` component. UI and web
source did not change, so the browser regression results below remain the
latest web verification; they were not repeated for this toolchain-only change.

No device was flashed. Hardware acceptance remains outstanding.

## Historical baseline: ESP-IDF 6.0.3

Migration verification on 2026-10-05:

| Check | Result |
| --- | --- |
| `tools/build.ps1` / `idf.py build` | PASS, ESP-IDF 6.0.3, ESP32-S3 |
| Firmware image and partition size check | PASS, `build/HisuOS.bin`, 0x166a30 bytes; app slot 0x1a9000; 16% free |
| `npm ci --ignore-scripts` | PASS, dependency installation |
| `npm run build` in web/src | PASS, GIF bundle regenerated |
| `test-gif-resize.mjs` | PASS, converter and home upload integration |
| `test-home-ai.mjs` | PASS, profile/key settings, ask flow and face-only behavior |
| Component structure and sibling dependency scan | PASS, seven components with explicit manifests/CMake boundaries |

Build-generated configuration confirms 16 MB flash, octal PSRAM, GIF support,
UART console and disabled native USB Serial/JTAG runtime. Compiler migration
required replacing removed ESP-IDF `json` with `espressif/cjson` 1.7.19.
Audio APIs now pass millisecond timeouts directly to I2S rather than converting
them into FreeRTOS ticks.

The SDK emits Kconfig notices from its BT/FATFS definitions; they did not prevent
the completed firmware build. Browser tests use mocked device API responses and
do not establish live HTTP/LLM operation on the board.

No device was flashed. Hardware boot, display, buttons, Wi-Fi provisioning,
PCM audio and battery readings still require the acceptance steps in
`boards/zhengchen_154/README.md`. ESP-Claw/voice/FriendLink integration is not
part of this baseline.
