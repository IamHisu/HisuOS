# Experimental Zing MP3 Playback

HisuOS searches and streams public 128 kbps MP3 tracks directly over Wi-Fi.
No PC, relay service, external music key or full-song download is required.
The source is unofficial and availability is not guaranteed.

## Voice Flow

- Ask Hisu for a song, preferably including the artist.
- Gemini calls `search_music`, compares up to five titles/artists, and calls
  `offer_music`. It speaks the selected title/artist and waits for confirmation
  in a new user turn before `play_music`. It must not invent IDs or substitute
  a different song after an error.
- Successful `play_music` means the exact ID is queued, not already resolved or
  audible. Queuing does no HTTP. Hisu closes/destroys the Gemini connection and
  releases its receive buffer before resolving metadata and downloading/decoding
  on the existing transport worker. Decoded PCM is copied into the existing
  output queue in PSRAM; the playback worker only feeds I2S. Source failures at
  this stage use the dismissible music-error screen.
- Voice teardown posts music-loading directly, never the wallpaper in between.
  The screen shows loading, then playing with the title and artist.
- Say Hi Su to cancel music and open voice chat, or press BOOT/Back to stop.
- Natural end of track drains PCM and the I2S ring, closes the speaker, releases
  music/network ownership, then opens Chat. Hi Su briefly announces that the song
  ended and asks whether to play another song or chat, then returns to listening.
  No repeated greeting or automatic song is generated. Normal listening timeout still
  applies. Playback errors remain dismissible; manual cancellation never reopens
  Chat by itself. A completed-ticket check fences a late finish against cancel,
  wake and newer playback.
- The resumed session receives bounded volatile metadata for the last song and
  up to five search results, explicitly marked as untrusted data. It is not a
  restoration of the old WebSocket or its entire conversation transcript.
- In this resumed context only, a new real user request such as "tiếp tục"
  authorizes Gemini to choose a different song and call `play_next_music`
  immediately without another confirmation. The tool rejects the last ID,
  unknown IDs, missing input and ordinary non-resumed sessions. A specifically
  named new song still follows the normal offer/confirm flow. Silence never
  starts a next song; AI interprets the request, not a local keyword matcher.

## Music Display

The 240x240 screen uses a black background, softly fading rainbow light,
96 thin white mirrored waveform strokes, a curved treble clef, six floating
single/beamed notes and a separate title/artist band. The existing UI task
updates it at 20 FPS; there is no image decoder, new framebuffer, task or FFT.
A lock-free meter samples the actual volume-scaled playback PCM after successful
speaker writes. Height and glow react to the amplitude envelope with a short
decay; stale/muted/starved audio decays to the quiet baseline. Note positions
continue floating over time. This is audio-reactive decoration, not an estimated
BPM or a measured frequency spectrum. Timers are deleted whenever the view exits.

`node tests/test_music_visual.mjs` renders the native geometry to Canvas and
checks silence/loudness/motion screenshots. It mirrors LVGL primitives and does
not replace testing TFT frame rate and audio on the physical device.

## Boundaries

- Only HTTPS streams on the configured Zing CDN suffixes are accepted. CDN
  301/302/303/307/308 responses may redirect at most three times. Every destination
  must pass the same URL allowlist before any connection opens. Absolute HTTPS,
  scheme-relative and origin-rooted Location values are supported; other relative
  forms fail closed. Missing, oversized or duplicate Location headers, credentials,
  arbitrary ports, fragments, control characters and unrelated hosts are rejected.
  The previous connection is closed before the next opens. API cookies are never
  forwarded to stream hosts, and signed redirect URLs are not logged.
- Paid, blocked or missing stream URLs are errors; there is no alternate-song fallback.
- Requests require a synchronized board clock. Cookies and signed URLs are
  temporary and are not persisted or logged.
- MP3 parsing/decoding uses Espressif's audio codec 2.6.2. The existing I2S owner
  handles both Gemini audio and music at the source sample rate (8-48 kHz, 16-bit,
  mono/stereo). Music is mixed to mono for the board's single speaker.
- No new task is added. Existing send/playback task stacks increase by 2 KiB
  each. The search body (at most 64 KiB), track, and encoded/PCM buffers live in
  PSRAM; the codec and HTTP/TLS client allocate additional runtime memory.
- Cancelled music must release the audio owner before a new Gemini connection
  starts, so delayed stream reads cannot block playback of the new voice reply.

## Jitter Buffer

Network reads and decoding no longer run in the same task that feeds the speaker.
The existing `live_send` worker becomes the music producer after WSS teardown;
`live_play` consumes the shared output queue. There is no new task or task stack.
Queued music PCM is capped at 128 KiB (plus at most one producer and one consumer
frame in flight). Queue pressure blocks the producer without dropping/replaying
PCM, and cancellation releases copies. The consumer retains the track until the
producer has stopped and all queued PCM has drained, including failure paths.

Music and Gemini initially target 500 ms of buffering with a 750 ms wait limit.
After starvation they target only 60 ms with an 80 ms wait limit, so an already
interrupted phrase does not incur another full startup delay. Completed short
responses/tracks bypass the wait.
This trades some startup latency for tolerance of network jitter; longer stalls
can still cause silence. Captions remain ordered with actual playback. The I2S
task has priority 6, above wake recognition, and no longer performs HTTP or codec
work. Its shared 1 KiB mixing workspace lives in PSRAM, not on the task stack.
The 2560-byte reserved DMA ring and zero-fill behavior are unchanged.
Wi-Fi performance mode stays active through music playback and is restored only
after draining/cancelling, before a new voice connection takes over.

Host transport tests cover bounded waits, short replies, cancellation, producer
vs. I2S ownership, exact PCM byte delivery, resolve/allocation/start-queue failure
and byte-budget/queue-pressure rollback. They cannot establish audible smoothness
or concurrent task timing on the physical board. Queue-starvation logs and the
producer stack high-water mark help collect hardware evidence. Voice starvation
logs include total received audio duration, wall-clock span, time since the last
PCM and maximum inter-arrival gap. These measure arrival timing, not whether a
gap originated at the access point, Internet path or model service.

## Verification

Search uses `/api/v2/search` with `type=song`, page 1 and five results;
`count`, `ctime`, `page`, `type` and `version` are signed in that order.
Results come from `data.items` (the parser also accepts legacy `data.songs`).
This avoids downloading artist, video and playlist sections from multi-search.
The serial monitor reports HTTP status, transport errors, received byte counts,
truncated/oversized bodies, invalid JSON and numeric upstream API errors.
It never prints cookies, signed URLs or response bodies.
API requests explicitly accept JSON with identity content encoding, but Zing was
observed returning gzip anyway. Gzip is decoded using ESP-IDF ROM miniz with
PSRAM output/workspace and a 64 KiB output ceiling. Header fields, header CRC
when present, stream completion, payload CRC32 and uncompressed size are checked;
unsupported encodings, concatenated/trailing streams and corrupt/oversized data
fail closed. HTML, JSON parse/allocation failures and invalid API envelopes have
separate diagnostics. Monitor output includes compressed/decompressed byte counts.
Host gzip tests exercise the wrapper and split HTTP delivery with a stored-block
DEFLATE mock; they do not execute the ESP32 ROM implementation.

Run `tools/test-voice.ps1` for protocol, tool handoff and music-worker tests.
For recommendations, Gemini can offer up to three distinct songs from its latest
search using `offer_music({ids:[...]})`. A specific requested song still uses
`offer_music({id:...})`. The response preserves numbered order with exact ids,
titles and artists. Firmware stores only the bounded list of three ids, and
allows playback of a member only after the offered reply finishes speaking and
the next real user turn supplies a non-overflowing transcript within 60 seconds.
Re-offering replaces the list; a new search, cancellation or new session invalidates
it. Single-track confirmation remains supported. Gemini interprets ordinal/title/
artist choices; firmware does not parse Vietnamese keywords or independently prove
that a transcript selects the id Gemini supplies. The prompt requires clarification
and re-offering on vague agreement, never defaulting to the first item. A clear
selection is the confirmation; it does not need another confirmation question.
Host tests validate list bounds, duplicates, unknown ids, order, every selected id,
replacement, missing spoken reply and late transcription, not model understanding.
The worker tests compile the actual implementation with HTTP, crypto, MP3 and
RTOS mocks; they do not establish hardware decoding quality or live Gemini selection.
Live endpoint/header checks and a successful firmware build are separate from
listening on the physical board. No automatic flash is performed.

A device log reached `/song/get/info` but failed creating the next socket
(`ESP_ERR_ESP_TLS_CANNOT_CREATE_SOCKET`) while Gemini was still connected.
Resolution now runs only after WSS teardown, reducing concurrent socket/TLS/RAM
pressure. That log alone cannot distinguish socket exhaustion from allocation
failure; API failures now include the immediate system errno and heap statistics.
No socket-limit increase or automatic network retry is added. Cancellation before
resolution does no HTTP, and resolution preserves the original queue ticket.

Playback logs mark decoder initialization, CDN connection, headers/status, the
first received data and decode/output startup. The stream reads 2 KiB at a time
to avoid waiting for an 8 KiB block before feeding the parser. A decoder
`BUFF_NOT_ENOUGH` result may expand the PSRAM PCM buffer up to 32 KiB and retry
the same unconsumed input; invalid sizes/consumption and allocation failure stop
cleanly. Errors report the stage, codec result and received/decoded byte counts
without printing the signed stream URL. Host tests cover expansion, its memory
failure, invalid growth requests, unsupported format, guarded redirects and
no-PCM EOF. A successful streaming API response alone does not prove CDN download
or MP3 playback has started on hardware.
