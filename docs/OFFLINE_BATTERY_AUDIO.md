# Offline Erinome Battery Alerts

The optional on-board preparation path uses the separate Reader key in Setting.
The PC generator still uses its documented environment variable independently.
The current default is original Erinome without effects. Existing Kore/Puck clips
are rejected, not relabeled; install the newly prepared audio image along with
the firmware. See [key routing](GEMINI_KEYS.md).

## Prepare on the PC

Set `GEMINI_DEV_API_KEY` (or `GEMINI_API_KEY`) in the PC environment, then run
`python tools/prepare-battery-audio.py` from HisuOS. Never pass the key as an
argument or commit it. PC generation requires the `websocket-client` Python package.
The tool requests four battery recordings plus one generic
reminder recording using Gemini Live with the same model as the firmware (not
the legacy TTS endpoint), caches WAV
previews and script metadata under `.cache/battery-audio/`, then atomically
publishes `battery_audio.bin` only after all five clips pass validation. Existing
matching clips are reused; `--pack-only` never contacts Gemini. Failed requests
stop without automatic retries or model fallback. The tool does not enable
billing or determine whether the supplied key belongs to a paid account.

Run `idf.py reconfigure build` after preparing the clips. The optional CMake
integration adds the 0x1e0000-byte audio image at storage-relative offset
0x206000 (currently absolute 0xa26000) to normal `idf.py flash`. It does not
write the wallpaper or memory regions and does not enlarge the app binary.
Do not use erase-flash. App-only OTA updates cannot install missing recordings.
If the image is absent, normal builds continue without new audio; existing
Erinome recordings already on the board remain usable. Old Kore/Puck records are rejected
by the voice CRC, not relabeled as Erinome. Erinome is used without EQ, pitch shifting
or other audio effects. Setting shows readiness only;
the preparation button has been removed. Reboot must show `5/5` before testing.

PC preparation uses Erinome Live, script CRC and the same PCM/header format as the
existing store. A host/firmware compatibility test packs synthetic audio and
loads it through the actual firmware validator; it does not test pronunciation.

## Legacy Device Preparation

The internal preparation API is retained for compatibility but is no longer
triggered by the Setting UI. It uses the existing `live_send` worker, not a new task. One requested
clip is generated at a time, with at least 22 seconds between jobs. The worker
uses Gemini 2.5 Flash Preview TTS `generateContent`, selects Erinome explicitly, and
requests five fixed Vietnamese scripts with natural delivery, without effects.
No conversation history or memory is sent. The API key stays in an HTTPS header,
never in URLs, browser responses or logs. Certificate verification stays enabled;
redirects and automatic model fallback are disabled. HTTP 429/quota, permission,
unsupported model and malformed response errors stop the batch for manual retry.
The firmware cannot determine whether an account has billing enabled; requests
consume the account's configured Gemini quota. It never enables billing.

Reference: Google's [legacy speech generation API](https://ai.google.dev/gemini-api/docs/generate-content/speech-generation)
documents the Erinome voice, supported TTS models and mono 24 kHz PCM output.
[Pricing](https://ai.google.dev/gemini-api/docs/pricing#gemini-2.5-flash-preview-tts)
currently lists a free tier for this model, but actual key access and quota must
be tested. The same voice name on TTS and Live does not guarantee identical
prosody. Dynamic online reminder speech still uses this TTS path, now with
Erinome; the pre-generated offline clips use Live. Neither route applies EQ,
pitch shifting or tempo effects. Recordings must be auditioned on the device.

## Offline Playback

Two variants are available for each stage:

- At/below 15%: a short request for charging, with a mild tired-robot joke.
- At/below 5%: a more urgent line, including the paperweight joke.

The stage must remain eligible for four seconds of idle observations. The
existing power service already averages ADC readings. Each stage plays once
per in-memory discharge cycle; playing the urgent stage also marks the earlier
stage done. Ten seconds of observed external power rearms the stages. Variant
selection uses a time-derived starting choice and avoids the previous choice
within a stage when both files are ready. State is not checkpointed while low;
reboot may allow another warning.

Playback runs on `live_play`, reusing the already reserved speaker DMA. It uses
256 mono samples at a time, caps volume at 50%, and respects speaker mute/zero
volume. It never opens HTTP or WebSocket. It waits while voice/music is active,
stops for a new wake, mute, charging or invalid power reading, and does not open
the amplifier at/below 2%. An interrupted/failed attempt waits 30 seconds before
retry. Successful alerts drain then disable the speaker. There is no automatic
power-off and no offline speech synthesis.

Percentages and charging detection rely on the current prototype ADC/GPIO9
profile. These are not measured brownout/undervoltage guarantees. Validate on
the actual board; do not intentionally discharge below a safe hardware limit.

## Storage and Ownership

Storage layout is unchanged up through the wallpaper and memory regions. Five
0x60000-byte slots follow memory at partition-relative offset 0x206000, ending
at 0x3e6000, within the existing 0x4e0000 storage partition. Schedule slots start
at that boundary and end at 0x3ea000. No partition-table
change or filesystem is needed. Each slot holds at most 384000 PCM bytes (eight
seconds), after a 4096-byte header sector. The versioned 64-byte header includes
voice/script fingerprints, sample length, PCM CRC and header CRC.

Valid clips are immutable: preparation never erases a ready slot. A missing
slot is erased, receives its payload, verifies payload CRC, then receives its
header last and reads it back. Boot verifies headers and full PCM CRC; partial
writes or corrupt/wrong-script records are not played and can be regenerated.
Prepared clips survive app updates while the slot layout and script version
stay unchanged. Changing scripts invalidates only their cached records.

An idle-maintenance reservation prevents local playback from starting during
flash writes or network preparation. Gemini connection startup and preparation
share one worker, so their TLS allocations do not overlap. New wake interrupts
the asynchronous HTTP job; no partially returned audio is stored. External
power and idle state are checked again before committing. Waking during the
short flash commit waits for that bounded operation to finish. Erase/write does
not hold the alert mutex. Firmware logs only clip index/error/HTTP status.

Preparation buffers are bounded in PSRAM: about 504 KiB HTTP JSON, its parsed
base64 string, and up to 375 KiB PCM. HTTP/TLS is cleaned up before audio parsing
and flash commit. PCM with unexpected MIME, incomplete/blocked response, excess
length, odd sample count or essentially silent output is rejected. There is no
new resident audio buffer or task stack. The 2048-byte stack-frame guard applies.

## Verification

`tools/test-alert.ps1` compiles actual alert policy/store/TTS preparation against
partition, HTTP, base64 and RTOS mocks. It covers thresholds, debounce, rearming,
variants, CRC/reboot, partial writes, strict Erinome request, bounded responses,
silence, cancellation, quota errors and cleanup. `tools/test-voice.ps1` covers
the shared speaker path, mute/critical-power suppression, maintenance exclusion
and wake interruption. `tests/test_battery_audio_host.py` checks PCM/WAV formats,
secret-safe API errors, flash offsets and compatibility with the C validator.
Browser tests exercise read-only readiness and the absence of the preparation
button at desktop, 390px and 320px widths. They do not prove API access, pronunciation, voice
quality, audible continuity, real charge detection or low-battery supply margin.

Board acceptance: generate all four on the PC, reconfigure/build and flash;
reboot to confirm readiness;
disable Wi-Fi and verify one warning per stage with no network log. Confirm mute,
plugging in, and calling Hi Su suppress/interrupt warnings, and verify existing
wallpaper and memory remain intact. Actual depletion tests need appropriate
hardware supervision; mocked thresholds do not require draining a physical cell.
