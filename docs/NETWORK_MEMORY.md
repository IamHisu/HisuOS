# Gemini connection memory

## Bluetooth Removed (2026-10-21)

BLE provisioning, NimBLE configuration, companion web and its workflow/tests
have been removed. `CONFIG_BT_ENABLED` is disabled in defaults and the local
generated config; CMake rejects an old config that enables it. The resulting
link map has no Hisu BLE, NimBLE or BT controller archives loaded.

Same-workspace build comparison: binary 3,063,488 -> 3,056,560 bytes (-6,928),
static DIRAM 199,191 -> 198,535 bytes (-656). BLE was not started at runtime
before removal, so this is not a claim of reclaiming a running host's task,
buffers or tens of KiB of measured heap. The historical device logs below
predate removal. Post-flash heap/stack measurements are still needed; no board
was flashed or reset for this change. JSON allocation, lookup guards and task
stack placement are unchanged in this BLE-only change.

## Lookup And UI Diagnostics (2026-10-21)

Lookup serialized cache and worker responses now allocate explicitly in PSRAM,
not with `cJSON_PrintUnformatted`. The bounded preallocated printer grows from
1 to 32 KiB and frees failed attempts. Replacement allocation failure preserves
the previous cache, query and expiry. Three cache slots and their TTLs are
unchanged. The short-lived worker stack increases from 6 to 8 KiB in PSRAM;
no permanent task is added. The consumer still reaps the suspended task using
`vTaskDeleteWithCaps`.

`LOOKUP: RAM phase=before_json_parse` and `after_json_parse` bracket provider
JSON parsing after HTTP/TLS cleanup. `worker_start` and `worker_cleaned` include
internal free/largest, PSRAM free and stack high-water bytes. `Complete` runs
after JSON serialization/deletion and body cleanup, before publication and
suspension: `stack_bytes=8192`, `stack_free`, `stack_low=1` below 1,536 bytes.
Cache logs include slot and text length, never locations, headlines or keys.

### Measured JSON Pressure And Scoped Fix

Device log `chat-20261021-165503-987351-COM8.log` confirmed weather JSON parse
snapshots falling to 47, 35 and 19 bytes of free internal RAM (largest block
24, 12 and 0), despite roughly 2.4 MB of free PSRAM. Live later reported a
historical internal minimum of 12 bytes. Cleanup recovered roughly 11-12 KiB;
this identifies transient allocation pressure, not proof of a long-term leak.
The observed lookup stack minimum was 3,976 bytes. Eleven lookups completed,
two news cache hits worked and none was rejected by the internal memory guard.
These figures are from before the scoped parser fix below, not measurements
of the corrected firmware.

Lookup now compiles the existing managed cJSON 1.7.19 source a second time with
all function symbols prefixed `hisu_lookup_` and default malloc/realloc fixed
to `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`. No upstream source is modified, no
new dependency or flash partition is added, and no global hook is switched.
Provider trees, cache-hit trees, result nodes and keys/strings all use this
instance. Missing PSRAM fails closed instead of falling back to internal RAM.
The private header must be included only by lookup units, never by Live or
other modules. Query arguments from Live are borrowed read-only, not attached
to or freed through private trees. Only serialized text crosses module bounds.

`send_lookup_result` delegates envelope creation to lookup instead of parsing
the full weather/result JSON back into a second normal-heap tree. Only JSON
already serialized by the worker can be embedded as raw JSON. The small
envelope tree, copied raw text and final escaped-ID envelope are all in PSRAM.
The response remains limited to 32 KiB; envelope capacity is bounded by that
limit plus worst-case escaping of the 127-byte call ID and fixed overhead.
`LOOKUP: Envelope ready` logs internal free/largest and live_send's stack
high-water after cleanup. A missing response becomes a structured memory error.
Other cJSON users (Live setup, music, memory, Setting) keep their normal
allocator. Do not introduce temporary `cJSON_InitHooks` swaps later.

Host tests verify the namespace against the dependency, coexistence with the
normal parser, external allocation capabilities, JSON parse OOM unwind over
640 allocation budgets, envelope OOM cleanup/ID escaping, cache preservation
and zero tracked external allocations left after tests. Build/link verification
checks the two implementations coexist. Actual internal free/largest during
parse and send still need the same board retest; no audio underrun fix is
claimed from this change alone.

UI logs one startup sequence with process-wide heap snapshots:

| Phase | What Just Ran |
| --- | --- |
| `before_lv_init` | Baseline inside UI task |
| `after_lv_init` | LVGL initialization |
| `after_display_create` | LVGL display allocation |
| `after_draw_buffer` | Internal DMA draw buffer allocation |
| `after_board_init` | Board/display driver setup |
| `after_splash_ui` | Boot splash object construction |
| `before_idle_ui`, `after_idle_ui` | First idle UI and uploaded media setup |

Each UI line has free internal/DMA/PSRAM, internal largest block and signed
`*_used_delta` from the prior sample (positive = net consumption, negative =
net release). Baselines have delta zero. DMA is a subset of internal memory:
do not add the two totals/deltas. The draw buffer is separately logged as
19,200 bytes (240 x 40 x 2), excluding allocation metadata and LCD driver
buffers. Other tasks can allocate/free between samples, so deltas are estimates
of net heap change, not exclusive LVGL accounting. Splash and idle are separate
screens; do not sum every phase to claim simultaneously live widget memory.
Allocator and DMA buffer placement remain unchanged. Logging is not per-frame.

### Internal Headroom Guards

| Lookup Stage | Minimum Internal Free | Minimum Largest Block |
| --- | --- | --- |
| Create worker | 10 KiB | 2 KiB |
| Before each HTTP request | 6 KiB | 2 KiB |

These unchanged thresholds are checks, not reservations or guarantees for TLS.
One lookup worker is allowed; music waits for lookup cleanup before resolving.
A shared boolean helper alone would not prevent check-then-allocate races.
Do not lower guards solely to eliminate `Rejected: internal_memory` logs.

### Remaining Stack Placement Audit

- `hisu_dns` currently uses an internal 4 KiB stack. It receives/replies to UDP,
  checks the associated AP client, calls the portal-active callback and closes
  its socket. No direct NVS/flash write was found on this path. Moving it is a
  candidate, but the self-delete/DONE/stop lifecycle must be adapted to the
  external-stack creation/deletion API and retested through hotspot restarts.
- `hisu_internet` currently uses an internal 8 KiB stack. It performs the two
  connectivity probes and publishes link state under the network mutex. No
  direct NVS write was found on that worker path. It is paused during Setting
  and requested/applied voice performance mode. TLS depth and board behavior
  still need measurement before changing stack placement.
- Do not confuse either with `hisu_network`, which saves credentials/settings,
  or `hisu_media`, which erases/writes flash. Their internal stacks stay put.

DNS/Internet stack placement, LVGL allocator and cJSON hooks have not changed.

## Board Test Checklist

Flash normally without erasing NVS/storage. Keep the current uploaded wallpaper.
Use USB Chat with volume zero and microphone muted if testing silently; do not
run another monitor on the same COM port while Chat holds it.

1. Capture startup from `UI: RAM phase=before_lv_init` through `after_idle_ui`.
   Confirm the current image/GIF and theme still display normally. Send the
   whole startup block, including the 19,200-byte draw-buffer line.
2. Open a chat and ask for today's afternoon rain in Ho Chi Minh City, current
   domestic gold prices, then today's technology/AI/economy news. Wait for each
   response before the next. Send `LOOKUP` and Live RAM/error lines for all three.
   Weather should have before/after parse samples for geocoding and forecast;
   news uses XML, so no provider-JSON parse pair is expected for it.
3. Repeat the same lookup arguments shortly afterward. If the model actually
   calls `lookup_info`, expect `Cache hit` instead of new HTTP for that topic.
   A conversational answer reusing context without a tool call is not a cache
   failure. Ask a different city/day to exercise cache replacement as well.
4. Repeat weather -> gold -> news three rounds, then close/reopen chat twice.
   Compare `worker_cleaned` with comparable warmed-up runs, not first boot:
   three PSRAM caches persist intentionally; internal free/largest should not
   steadily collapse. Investigate `stack_low=1`, allocation/TLS failures or a
   repeatable downward trend; do not infer a leak from one differing sample.
5. Cancel/BOOT back while a lookup is pending, reopen chat and ask again. Old
   results must not appear in the new session, and lookup must become usable.
6. Optional: disconnect/reconnect the router, then open/close Setting hotspot
   twice and return to chat. This is regression coverage, not a stack move.

No audible playback test is required for this RAM measurement pass. Actual
speech/music smoothness and timing still require a separate audio test later.
Host mocks validate capability requests, limits, failure paths and ownership,
but cannot measure real SRAM, TLS stack depth, fragmentation or UI frame rate.

## Existing Network Profile

An existing `sdkconfig` overrides `sdkconfig.defaults`. Updating the defaults or
running an ordinary build does not replace old values. The CMake configuration
now rejects an obsolete network memory profile before producing firmware.

The intended profile prefers PSRAM for ordinary allocations larger than 2 KiB,
reserves 64 KiB for allocations requiring internal/DMA memory, enables PSRAM
allocation for Wi-Fi/LwIP, and uses eight static Wi-Fi TX buffers. Keep TLS in
PSRAM, software AES and the 16 KiB TCP send budget. See `sdkconfig.defaults` for
the exact settings. The reserve is an allocation policy, not a promise that
64 KiB will remain free after internal-only tasks and drivers start.

The October 20 device log repeatedly timed out sending the 24,570-byte Gemini
setup message. At failure, internal free memory was 3,683 or 8,223 bytes, with
largest blocks of 1,472 or 1,536 bytes, while PSRAM still had over 2.5 MB free.
This demonstrates internal memory pressure, not exhaustion of total RAM. It
does not by itself prove that every network timeout is caused by allocation
failure. The log did not show a Wi-Fi disconnect or certificate failure.

After flashing the corrected build, compare `Speaker ready RAM`,
`WSS connected RAM` and `WSS failure RAM` across repeated sessions. Verify setup
completes, microphone turns reach Gemini and music starts after confirmation.
A stable build and configuration checks cannot establish runtime free memory,
network reliability, or audible playback without testing the physical device.
