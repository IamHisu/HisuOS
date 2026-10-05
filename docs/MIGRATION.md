# Migration record

The source projects remain intact. HisuOS contains source copies, not symlinks,
override paths or build-time references to its siblings.

| Source | Destination | Treatment |
| --- | --- | --- |
| FriendOS/main/main.c | hisu_app/src/hisu_app.c | App entry extracted; callback bindings added |
| FriendOS/main/board | hisu_board + boards/zhengchen_154 | Drivers retained; pin map separated |
| FriendOS/main/ui | hisu_ui/src, fonts, storage | Face/media/subtitle behavior retained |
| FriendOS/main/network/friend_wifi_store.* | hisu_settings | Saved-network persistence |
| FriendOS/main/network/friend_wifi.* | hisu_wifi | HTTP dependency replaced with callback |
| FriendOS/main/network/friend_http.* | hisu_web | Existing API and embedded assets retained |
| FriendOS/main/ai | hisu_ai | Text providers and queues; UI dependency replaced with output callbacks |
| FriendOS/main/web + web-src | web/assets + web/src | Bundler/test paths updated |
| FriendOS/partitions.csv | partitions.csv | Existing two-slot media layout retained |
| Muse board_zhengchen_154.c | hisu_board/board_speaker.c, board_power.c | Standalone I2S speaker and raw-ADC calibration adapted |
| Muse Zhengchen SDK profile | boards/zhengchen_154/sdkconfig.defaults | Flash/PSRAM/console hardware settings selected |

The microphone PCM conversion follows the Muse board's 32-bit-to-PCM16 layout.
It exports mono frames; the speaker API accepts interleaved stereo PCM16 at
16000 Hz. Consumers must explicitly duplicate/resample channels as needed.

Muse cloud pairing, SDK token, account binding, tunnels, OTA backend and its
application lifecycle are not imported. Muse's settings UI depends on those
services, so the independent FriendOS Web Settings is the active baseline.
ESP-Claw, GMF, WebRTC and Board Manager are not added as dependencies yet.

## Provenance

Source snapshot: local working trees during this migration, including local
changes. Existing comments and imported font/assets notices are retained.
Muse-derived source retains its Meta copyright and Apache 2.0 notice; see
`licenses/Muse-Apache-2.0.txt`. See `licenses/NOTICE.md` for additional details.
No blanket license is assigned to the entire project by this migration.

## Compatibility

FriendOS's historical build used ESP-IDF 5.5.5. HisuOS was initially built on
6.0.3, then returned to 5.5.5 at the user's request. The manifest now requires
exactly 5.5.5, and JSON uses that release's built-in `json` component instead
of the standalone cJSON dependency. LVGL 9.4.0 and button 4.2.1 remain
pinned to the versions inherited from FriendOS. A firmware build checks compile
compatibility; it does not establish display, microphone, speaker or battery
behavior on hardware.
