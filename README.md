# HisuOS

Standalone ESP-IDF firmware for Hisu, starting with the Zhengchen 1.54-inch
ESP32-S3 board. Selected FriendOS modules are reorganized as reusable components;
Muse's Zhengchen speaker and battery logic are adapted without its cloud runtime.

## Layout

```text
HisuOS/
  main/                       Firmware entry point only
  boards/zhengchen_154/        Pins and hardware sdkconfig defaults
  components/
    hisu_app/                 Boot, task ownership, service wiring
    hisu_board/               Display, buttons, microphone, speaker, power
    hisu_settings/            Saved Wi-Fi credentials
    hisu_wifi/                Connection manager and setup AP
    hisu_ui/                  Face, GIF, images, subtitles, media storage, fonts
    hisu_ai/                  Text providers, profile, request/result queues
    hisu_web/                 HTTP settings and chat endpoints
  web/assets/                 Files embedded in firmware
  web/src/                    GIF bundler and browser regression tests
  tools/                      Local build helpers
  docs/                       Architecture, migration, hardware checklist
  licenses/                   Notices for reused third-party code
```

## Build

Required toolchain: ESP-IDF 5.5.5, target ESP32-S3. Activate ESP-IDF 5.5.5, then run
from this directory:

```powershell
idf.py build
```

Alternatively, from a normal PowerShell session:

```powershell
./tools/build.ps1
```

The helper accepts only 5.5.5: it uses a matching active environment, EIM
installation or `.cache/esp-idf-v5.5.5` with the existing EIM Python environment.
Pass `-ActivationScript` for another installation layout. It does not flash.
`managed_components` and `sdkconfig` are generated locally;
board defaults and manifests are the maintained configuration. Dependency
versions are pinned, and the resolved `dependencies.lock` belongs in source control.
No sibling project is needed for a clean checkout or build.

The 5.5.5 firmware build has passed. The earlier 6.0.3 build/config are retained
locally as `build-idf6.0.3` and `sdkconfig.idf6.0.3` for comparison.

## Current behavior

- LVGL face animation, RGB565 images, GIF upload and Vietnamese subtitles.
- Saved-network connection, setup AP, Web Settings, profile and text chat.
- BOOT click toggles face/media; BOOT held for five seconds enters Wi-Fi setup.
- Raw microphone PCM API, speaker PCM API, battery status API.
- Speaker and battery logic still need validation on the physical board.

Open the device's LAN IP in a browser once it connects. For initial setup, join
`Hisu-Setup` and open `http://192.168.4.1`. These pages require the device HTTP
API; opening the HTML directly is only useful for frontend tests.

Realtime voice, STT/TTS, ESP-Claw, long-term memory, MCP and FriendLink are
future integrations, not active features of this baseline.

## Web development

```powershell
cd web/src
npm ci
npm run build
npm test
```

The tests use installed Chrome; set `CHROME_PATH` for a different Chromium
installation. Rebuild firmware after changing embedded assets.

See [architecture](docs/ARCHITECTURE.md), [migration](docs/MIGRATION.md),
[board notes](boards/zhengchen_154/README.md) and
[validation](docs/VALIDATION.md) before expanding the project.
