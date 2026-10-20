# Spoken Wake Feedback

Wake acceptance queues one of three short Leda Live acknowledgments from flash.
They rotate without a flash write. A fourth clip reports connection failure and
directs the user to the specific on-screen error. These are recordings, not a
network request per wake. Missing/corrupt clips do not fall back to a beep.
The regular Gemini greeting still follows once setup is ready; early microphone
capture and removing that greeting are not part of this change.

Playback uses live_play, its existing reserved I2S channel and stereo workspace,
plus a 512-byte mono read buffer. Capture is gated while feedback is pending or
playing. Volume/mute and critical-battery safeguards apply. A changed epoch or
generation cancels old feedback. A cue acknowledges wake, not microphone readiness.

`tools/prepare-feedback-audio.py` uses GEMINI_DEV_API_KEY on the PC, generates four
Leda Live clips with Southern Vietnamese pronunciation instructions, and stops
on failure without retrying a paid model or rotating keys. `--pack-only` makes no
API calls. Accent and generated content still need listening verification.

Image: `.cache/feedback-audio/feedback_audio.bin`, four 0x3c000-byte slots at
storage-relative 0x3ea000 (absolute 0xc0a000 for the current partition table).
This is after schedule A/B and does not move wallpaper, memory, alerts or schedule.
Headers bind voice/style, exact phrase, length and PCM CRC; boot validates clips.
No whole clip is retained in RAM and firmware never writes this region.
Reconfigure/build then normal full flash installs the optional image; app-only
flash does not. No erase-flash is needed. Keep this ignored cache with local builds.

Wake now requests Wi-Fi performance before transport startup. Setup construction
overlaps the asynchronous WSS connection. The compact persona retains all tool
definitions; Reader/Interpreter omit personal/recent memory context.

One `LIVE: Start timing` line reports milliseconds from accepted wake: UI enqueue,
performance request/applied, WSS start/connected, setup sent/ready, audio received,
first successful speaker write, mic eligibility and cue write, plus setup build,
send time and bytes. -1 means the phase was not reached. WSS connected includes
DNS/TCP/TLS/HTTP; it is not a separate TLS measurement. These logs do not include
credentials. Real board latency still needs measurement.

No change to 500 ms audio prebuffer, 15 s setup timeout, AES configuration or
connection retries. Early capture, warm connections and dynamic tool loading
remain deferred pending measurements.
