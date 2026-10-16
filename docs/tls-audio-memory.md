# TLS and Speaker Memory

The ESP32-S3 hardware AES path uses DMA-capable internal RAM for descriptors and
temporary buffers even with `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`. The IDF 5.5.5
driver can allocate input/output bounce buffers in 1600-byte chunks for unaligned
PSRAM. Speaker startup also allocates internal I2S DMA buffers (the default six
240-frame stereo/16-bit buffers alone use 5760 bytes).

An observed device run reported 16927 bytes of free internal RAM before audio,
then `esp-aes: Failed to allocate memory` immediately after first audio, followed
by a TLS read failure and WebSocket disconnect. This confirms an AES allocation
failure; it does not prove the precise allocation or all transient memory users.

HisuOS disables hardware AES acceleration, using mbedTLS software AES instead.
TLS, AES encryption, certificate verification and hardware SHA/MPI stay enabled.
This avoids the hardware AES DMA allocation path without removing expressive
eyes/subtitles. The tradeoff is additional CPU
work, which needs verification on the board under simultaneous speech/display
load. Host tests cannot measure this cost or establish that all low-memory issues
are resolved.

`Speaker ready RAM` and `WSS failure RAM` report internal/DMA free memory and
largest blocks alongside remaining PSRAM. Keep both messages when collecting a
new device log. `tools/test-voice.ps1` checks the durable default and, when present,
the active sdkconfig to catch a stale configuration retaining hardware AES.

## Speaker Startup During Music Search

A later device log reports I2S DMA allocation failure when Gemini audio arrives
while a music-search HTTPS request is still running. The first mitigation used
four 160-frame stereo/16-bit buffers: 2560 bytes rather than 5760, excluding driver
metadata. PCM waiting for playback remains in PSRAM. This ring holds about 27 ms
at 24 kHz and 13 ms at 48 kHz; the smaller scheduling margin needs listening tests
on the board. Sample rate, volume scaling and zero-fill on underrun are unchanged.
Reducing the ring alone did not prevent a later DMA allocation failure. The
channel is now initialized during voice startup, before WSS/music requests, and
its DMA stays reserved between replies. Idle/finished playback disables I2S;
the channel is only deleted during startup rollback/deinitialization. Music and
Gemini change clocks on the disabled channel without reallocating DMA. Before
each enable, the entire ring is preloaded with silence to prevent stale audio
after interruption. Idle internal memory use therefore rises by the reserved
ring and driver metadata, while reply startup no longer competes for new DMA.
Failed initialization deletes the channel and reports heap statistics. Host
tests check dimensions, failure cleanup, reservation release, reuse, clock-change
failure and silence preload. They cannot establish the peak RAM margin or sound
quality under real concurrent TLS.

The later jitter-buffer change separates network/decode from I2S with the two
existing workers and PSRAM queuing instead of increasing this DMA reservation.
It adds roughly 500 ms of startup buffering and raises I2S task priority; audible
results and microphone timing still require a board test.

## Mono Microphone and Scheduling Margin

The Zhengchen board microphone now uses the left 32-bit I2S slot, matching the
device logs (left active, right zero), rather than reading both slots and picking
one by energy every block. Its default six 240-frame DMA buffers fall from 11520
to 5760 bytes. Raw/converted microphone buffers are explicitly allocated in
PSRAM before either worker starts. A different board wiring must set
`MIC_SLOT_MASK` to its actual slot.

The speaker ring is now six 240-frame stereo/16-bit buffers (5760 bytes), still
reserved before TLS. This restores 60 ms at 24 kHz and about 33 ms at 44.1 kHz.
Relative to the previous stereo-mic/small-speaker combination, the DMA payload
budget decreases by 2560 bytes. Driver metadata and transient TLS allocations
are additional; the real peak margin and audible improvement require a board
test. The 500 ms startup buffer and bounded short resume wait are unchanged.

AFE allocation explicitly prefers PSRAM and logs internal heap before/after
creation. Microphone and detection logs include minimum free stack in bytes.
The input preroll is 600 ms; sustained-speech detection remains 200 ms. No VAD
cache is concatenated, avoiding accidental duplication of samples already in
the preroll.

Startup rollback destroys AFE, command tables, MultiNet and model mappings, as
well as I2S and allocated buffers. Both application workers wait for startup to
finish, so rollback cannot delete a worker in the middle of a heap operation.
An AFE fetch fault closes the voice session and requests microphone shutdown;
faulted AFE/model resources remain retained until reboot rather than being
freed underneath an in-flight feed. Automatic AFE restart is not implemented.

Settings getters now use a brief cache-only critical section, not the mutex held
across NVS writes. Cache publication happens only after successful commit; flash
operations never run inside that critical section. This removes mutex-induced
audio waits but does not eliminate flash/cache stalls themselves. TCP receive
window changes, larger response queues and WebSocket callback backpressure are
deferred pending device timing/heap measurements.

## Uplink Write Stalls

A subsequent run no longer reported AES allocation failures. Instead, audio
uploads regularly took 100..350 ms, the input queue grew, and a socket write poll
timed out at about 1000 ms, disconnecting the session. That log establishes a
write stall, not whether its underlying cause is the AP, Internet path or TCP
buffering.

The maximum 100 ms audio JSON is larger than the old 4096-byte WebSocket buffer.
For example, 1536 samples expand to 4096 base64 bytes *before* the JSON envelope,
so the previous client split that message across WebSocket frames. The buffer is
now 8192 bytes with a compile-time assertion that audio JSON fits. Both client
buffers are large enough to use the configured PSRAM-preferred allocation path.

The app owns an external TLS/WebSocket transport using public IDF APIs. It keeps
the certificate bundle, Gemini endpoint/path and API-key header, and propagates
control frames for the client's ping/pong handling. After connection, it enables
`TCP_NODELAY` on that transport's socket to avoid Nagle buffering of small writes.
This is a latency optimization, not proof that Nagle caused the observed stall.
Transport creation failures clean up both layers; final cleanup happens only
after stopping/destroying the WebSocket client. Managed components are unchanged.

Write timeout is now 5000 ms, still bounded. Failed or short writes are never
retried because replaying a partially sent frame may duplicate/corrupt audio.
Existing turn/epoch checks discard obsolete queued frames after interruption.
The input queue, PCM format, and 100 ms batching limit have not been enlarged.
The TCP send budget is 16384 bytes instead of the default 5760. At roughly
43 kB/s for 16 kHz PCM/base64, a small send budget can become a throughput limit
on a high-RTT path. This is a shared lwIP default, not an increase in the voice
application queue; it permits more in-flight TCP data and may increase transient
packet memory use. Existing PSRAM-preferred lwIP allocation stays enabled. This
SDK's socket implementation has no supported per-socket send-buffer setter, so
the change is made in sdkconfig and the durable defaults, checked by host tests.
The normal batch target stays consistent with Google's [Live API guidance](https://ai.google.dev/gemini-api/docs/live-api/best-practices).

Host tests cover certificate/header/path retention, control-frame configuration,
socket option, cleanup on each initialization failure, timeout conversion and
no retry on failed/short writes. Real throughput and delay need a board test.

## Bounded Downlink Backpressure

Reply PCM retains the 512 KiB/96-item limits. When the byte budget or output
queue is temporarily full, the receive path retries every 10 ms for at most
100 ms, releasing reservations after failed enqueue and checking epoch/turn
cancellation each time. There is no indefinite socket pause and no dropping
old samples or moving subtitle/control markers. Prolonged overload still fails
the current session explicitly; this is not lossless recovery from sustained
overload. Control/caption enqueue failures retain their existing failure policy.
Host tests cover byte/slot pressure, recovery, timeout, cancellation and accounting.

The reminder TLS path uses a small atomic network-link snapshot, not `aps[12]`
on each nested stack frame. Logs report stack low-water marks after TTS and WSS
cleanup and flash-save duration for correlation with mic fetch gaps. A worker
change alone cannot guarantee flash operations will not delay audio. No extra
task or stack allocation was added; real stack headroom still requires hardware.
