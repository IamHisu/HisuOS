# Voice Transport Stack

A device log reported `A stack overflow in task live_send` after HTTPS certificate
validation. The user confirmed that the device was finding/playing music. The
corrupted backtrace identifies the task but cannot identify the precise nested
function that exhausted its stack.

The music HTTP path previously retained signing buffers and URL storage (about
1.6 KiB), a 541-byte encoded query in search, and transport audio/control frames
on stack while nested calls could perform TLS. The transport worker now owns a
PSRAM workspace containing its current and lookahead input frames, API-key and
header buffers. The music request owns the query/signing/URL scratch in PSRAM.
Each request has separate scratch; the playback worker does not share it.

The live_send stack stays at 8192 bytes: no extra internal stack allocation or
external task stack is introduced. Startup allocation failure rolls back all
workspace/buffer allocations. Invalid music queries are rejected before opening
HTTP, and failure to allocate a music request clears pending playback state.

The voice component emits GCC `.su` reports (`-fstack-usage`) and rejects individual
compiler stack frames larger than 2048 bytes. This only bounds individual frames,
not nested call-chain depth. The ESP32-S3 build after this change reports:

| Function | Own Stack Frame |
| --- | ---: |
| transport_task (including inlined callees) | 1024 bytes |
| music api | 64 bytes |
| hisu_music_search | 48 bytes |
| hisu_music_prepare | 64 bytes |

The task logs `Device tool start/finished: live_send minimum stack free=... bytes`.
IDF's high-water-mark API reports the minimum unused stack since task creation in
bytes; it does not reset per call. Real TLS/HTTP stack depth and headroom must be
verified on the board by finding/playing several songs after flashing.

Host tests exercise the actual music and transport source with HTTP/TLS/RTOS
mocks: signing inputs, parsing, playback/cancellation, invalid queries, workspace
allocation failures and cleanup, and independent current/lookahead frames. They
cannot reproduce the ESP32 task's runtime stack consumption.
