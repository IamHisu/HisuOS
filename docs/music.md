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
- Say Hi Su or press BOOT to cancel music and open voice chat. During audio,
  BOOT acts on its debounced press (30 ms), not release/hold; its subsequent
  release/hold is consumed so it cannot immediately close the new chat.
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
48 colored upward bars anchored to physical row 239 and three to five active
note/firework lifecycles, without a treble clef. A soft rainbow wash sits behind
the bars and fades upward; its height follows nearby bar height and PCM energy.
Notes rise from
pseudo-random lower positions to independently selected heights; the flight,
color, size and note glow are time-driven, not amplitude bobbing. Once at target
height they wait for an audio onset. At most four notes burst together into thirty-two
colored streaks each, with independent initial velocities, downward gravity
and a 750 ms fade. Wider dispersion, 100 ms tails, a colored core and white
spark tips make the burst visible at 240x240. The cap is 128 streaks, stored in
compact 16-byte records. The frame stays below 3 KiB and is allocated once in
PSRAM, reused only by the UI task rather than placed on its stack. Allocation
failure skips the visualization without interrupting audio; a later music view
can retry allocation. These are falling
fireworks, not a stationary flower/ring.
An energy-attack heuristic (minimum level, rise above the previous level and
smoothed floor, 240 ms refractory period) triggers bursts; this is not BPM or
frequency analysis. After 1.2 seconds without a qualifying onset, an eligible
note fades and respawns instead of fabricating a beat. The first three slots
respawn immediately; two extra slots have randomized rest periods. All slots,
particles and scene state are bounded; no task or per-frame heap is added.
Title/artist occupy rows 22-65, above the visualization at rows 72-239. There
is no fixed middle line or mirrored waveform. The existing UI task
requests updates every 50 ms; actual rendering speed needs board verification
at the higher particle count. There is no image decoder, new framebuffer, task or FFT.
A lock-free meter samples the actual volume-scaled playback PCM after successful
speaker writes. Height and glow react to the amplitude envelope with a short
decay; stale/muted/starved audio decays to the quiet baseline. Note positions
change position with time, while burst timing reacts to energy attacks. This is audio-reactive decoration, not an estimated
BPM or a measured frequency spectrum. Timers are deleted whenever the view exits.

Vol+/Vol- adjust by five points during music, Chat, and stories; held keys
repeat using the existing board repeat policy. A vertical right-side slider
and numeric level appear for 1.8 seconds. The top-layer overlay survives
subtitle redraws, hides on exit, and reuses its objects. Speaker-disable remains
independent: changing volume does not silently override it.

Button changes only update the cached RAM volume. The existing transport's
idle-maintenance path persists the final value, at most once per five seconds;
it never commits each press during playback. Resetting before an idle save can
lose the last adjustment. Revision checks preserve a newer button change if
an NVS commit is in progress. Host tests cover clamping, deferred writes,
failed commits and concurrent updates.

Music checks cancellation between every 256 output frames and closes I2S on
the playback worker as soon as it observes the canceled ticket, even if HTTP
cleanup is still pending. No UI task calls I2S or waits for network teardown.
Real stop latency still includes button polling, an in-flight I2S write/DMA
and LCD scheduling; host tests do not measure physical latency.

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
it. Single-track confirmation remains supported. For multiple songs, play_music
accepts the selected id alone. Firmware maps it to the offered position and
validates the actual received full user transcript. Legacy position and
selection_text fields remain accepted, but a supplied position must match;
a model-provided transcript never grants authorization. A conservative evidence
validator accepts explicit ordinal choices or an unambiguous full title/artist,
with common selection prefixes and polite suffixes. It never substring-matches a
shared artist or interprets generic assent as a choice. Negative, conditional,
quoted, multi-choice or unknown forms fail closed and require re-offering and
clarification. Matching folds ASCII case and whitespace; Vietnamese UTF-8 accents
remain intact. Unaccented ordinal forms are supported, but arbitrary spelling,
accent/case variations and complex language may need clarification. This is an
authorization check, not a substitute for Gemini's conversational understanding or
a guarantee that speech recognition is correct. An explicit request such as "chọn đại 1 bài", "bạn chọn đi" or "bài nào cũng được" also authorizes choosing any member of the spoken offer, with the same offered-id and actual-transcript checks. Generic assent still does not choose a song. A clear supported selection is
the confirmation; it does not need another confirmation question. Supported
ordinal forms include "bài số 3", "bài số ba", "bài ba", "số 3" and "3".
After rejection, the model must call offer_music again before asking a new
confirmation question: a spoken question alone does not renew the offer.
Host tests validate list bounds, duplicates, unknown ids, order, every selected id,
replacement, missing spoken reply, late transcription, evidence mismatch, vague
assent, shared identifiers and negative/conditional choices, not model understanding.
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

## Missing Offers And Repeated Playback Calls

Search results do not authorize playback. Both the search result and setup
explicitly require `offer_music` before reading a numbered list or asking for
confirmation. If the model skips this registration, playback stays rejected;
the result directs it to offer the exact id and wait for one new confirmation,
not retry playback or claim it is playing. A registered list accepts a clear
selection as confirmation without another yes/no question. The conservative
selection evidence grammar also accepts polite endings such as `đi bạn` and
`nha bạn`, but still rejects negation, questions and compound alternatives.

After three failed play calls in one real user turn, the transport suppresses
remaining audio/captions and fences that server response using the existing
obsolete-turn mechanism. It returns the frontend to listening, drops further
tool calls from the canceled response, and keeps WSS unless the normal cancel
timeout expires. New input resets the retry budget. This bounds a model loop;
it does not fabricate consent or filter repeated text by substring.

Host tests reproduce missing-offer recovery, a clear list selection, repeated
failed plays with distinct call ids, and a completion racing the final tool
result. They verify suppressed late output and a working next text turn.
