# Gemini connection memory

## Receive Pipeline (2026-10-22)

Live retains direct Gemini PCM/base64; no Opus relay or Xiaozhi dependency is
introduced. WSS assembles fragmented messages in the existing 128 KiB PSRAM
buffer, then copies complete messages into a bounded PSRAM ingress ring.
`live_decode` parses JSON and base64 in wire order before handing PCM/captions
to the existing `live_play` worker. Only `live_send` writes to WSS.

Ingress permits 32 queued messages and 384 KiB total payload, including the
message currently being decoded. Payload is allocated on demand, not reserved
at startup. The descriptor ring and an 8 KiB decoder stack are in PSRAM; the
task control block still needs internal memory. The JSON tree and decoded PCM
temporarily coexist with the input message, so 384 KiB is not a total pipeline
memory budget. Existing PCM queue limits and I2S DMA configuration are unchanged.

At ingress saturation the callback waits in cancellable 10 ms steps, applying
TCP backpressure. Three seconds without ingress drain progress fails the
session; allocation failure also fails explicitly. Mixed tool/control messages
must not be dropped to make space. Close/error notifications share the ring
and cannot overtake a queued goAway or turnComplete. After stopping WSS, the
sender pauses/drains ingress and waits for in-flight decode before handoff.
No queued item borrows the SDK buffer or stack-local connection.

`Receive pipeline` at WSS cleanup logs peak ingress bytes/messages and decoder
stack high-water bytes. Test normal chat, interrupt/BOOT, reconnect, music
handoff and return, reader and interpreter transitions. Watch for a steadily
falling warmed-up PSRAM baseline, decoder stack below 1,536 bytes, allocation
errors or sustained ingress saturation. Host tests cover framing, ownership,
ordering, canceled turns, OOM, limits/backpressure and startup rollback, not
real hardware stack depth or scheduling.

Voice starts a new session at 500 ms (750 ms wait cap). After underrun, the
resume target uses the larger of 800/1,200/2,000 ms progression, the last
observed receive gap plus 200 ms, and learned startup plus 300 ms; it is capped
at 2,000 ms. A troubled reply teaches the next reply's startup buffer, capped
at 1,500 ms. Each clean completed reply reduces that by 250 ms toward 500 ms;
new sessions reset it. Learned startup/rebuffer waits allow 750 ms above their
target. Finished tails drain immediately, cancellation is checked every 10 ms,
and a near-full output slot budget releases playback even below the time target
so tiny PCM/caption items cannot deadlock the producer. Music retains its
500 ms startup and 300/500/800 ms resume policy from the first patch.

`Voice buffer ready` logs target/actual hold, completion and slot pressure.
This trades additional delay after starvation for fewer short fragments, but
cannot mask an indefinitely slow network/server. `Voice queue starved` logs
the next target; `max_rx_gap` includes decoder scheduling/backpressure and must
not be treated as a pure network measurement.

### Music SRAM Pressure Follow-Up

The October 22 log had 37 voice underruns without ingress saturation (peak
51,518 bytes/six messages, decoder stack free 4,464 bytes). Following music,
internal low-water was 12 bytes although current free had recovered to 12,435.
This is pressure evidence, not proof of a leak or a single responsible phase.

Music provider JSON now reuses the existing fixed-PSRAM cJSON instance through
narrow parse/delete wrappers. No global hooks change; returned provider nodes
are only read, never attached to ordinary Live/tool result trees. Extracted
title/artist are copied to the track and the full info tree is destroyed before
opening the streaming API's TLS connection. Music HTTP RX/TX buffers are 4 KiB
each, above the configured ALWAYSINTERNAL=2 KiB cutoff, so ordinary IDF malloc
prefers PSRAM rather than requesting two 2 KiB internal buffers. This preference
does not eliminate every internal allocation or guarantee heap headroom.

`MUSIC: JSON before parse` / `after parse` log internal free/min/largest and
PSRAM. Reboot before retesting to establish a fresh historical minimum; alternate
chat -> search/play -> BOOT back to chat five times. Compare warmed-up current
free/largest and minimum at the same phases; send these logs with underrun and
buffer-ready lines. Host tests check explicit PSRAM capabilities, allocator
separation, freeing metadata before stream HTTP, OOM cleanup across allocation
budgets, learned buffer caps/reset/decay, cancellation and full-slot escape.
Actual reduction in pressure and audible improvement still require the board.

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

October 22 lookup hardening: HTTP RX/TX buffers are now 4096 bytes each, above
the 2048-byte ALWAYSINTERNAL preference, so ordinary HTTP allocation can use
PSRAM rather than consuming two 1 KiB internal buffers. The internal-memory
start/continuation guards remain unchanged. HTTP logs distinguish elapsed time,
deadline expiry, socket errno and body overflow; WSS teardown logs its stop and
decoder-drain phases. These logs diagnose stalls, not proof that every timeout
or teardown problem is solved.

Identical source/news failures are cached in PSRAM for 15 seconds after failure
to prevent repeated slow requests in one response. Cache entries are not stored
after cancellation/disconnection. Successful data retains its existing TTL.
Failed lookup envelopes parse only the small failure result to remove internal
codes; successful weather/news envelopes still avoid building a second JSON tree.

Read-only desktop probes on October 22 returned HTTP 200 for gold, hourly
weather and RSS; the actual host C parsers accepted all three live payloads.
NCT search/detail/lyrics returned HTTP 200, and a 128 kbps MP3 range returned
206 with valid frames after its ID3 tag. This does not measure board TLS/SRAM,
audible playback or jitter. The geocoder returned no match for `Quan 7` alone;
ask for the city when needed, and never label a city forecast district-specific.

Music list recovery retains at most 2 KiB of played captions in PSRAM, only
after a search in the current session, plus a temporary 2 KiB PSRAM snapshot
on a play request. It stores nothing to flash and adds no task. LRC storage is
bounded to 32 KiB text plus 256 entries in PSRAM; its optional 64 KiB download
buffer is freed before MP3 streaming. The current/next lyric viewport adds
three small LVGL objects (viewport, outgoing and next labels); next-line queue
payloads use owned PSRAM strings, not a second large inline SRAM event array.
