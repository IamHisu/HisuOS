# Bedtime Stories

Ask Hi Su in Vietnamese to tell a bedtime story. `start_sleep_story` defaults to
30 minutes; an explicit integer from 1 to 120 minutes overrides it. Ordinary
story requests need not start this mode. There is no new background task,
recording store, persisted timer, or change to saved volume/brightness.

The dedicated Reader key opens a new Gemini Live connection for bedtime mode;
configure it separately in Setting. See [key routing](GEMINI_KEYS.md).
It narrates in gentle Vietnamese using original
Erinome, in approximately 100-180 word segments. After each segment's PCM and final
DMA drain, playback queues one continuation. It does not reopen microphone
capture or ask the listener to say continue between segments. An automatic turn
cannot reset the timer or invoke side-effect tools; only expression changes are
allowed. Story content and pacing depend on the model.

The board's monotonic timer runs independently of the model and transport task.
Playback checks it between queue polls and PCM writes. At the deadline, audio
stops, pending output becomes stale, the conversation closes and the screen
backlight switches off. The deadline can cut a sentence short; this is not
guaranteed sentence-boundary finishing. Scheduling/I2S may add a short delay.
The screen-sleep request is latched separately from the bounded subtitle queue.
A later status or key interaction may wake the screen normally.

The board stays powered: alarms/reminders and normal low-battery policy remain
active and may display/play notifications. A deliberate wake or physical voice
interrupt cancels bedtime mode and resumes the normal conversation flow. A
spoken stop handled by `end_conversation` closes after the final reply and sleeps
the screen. Closing/reopening a session or rebooting clears the timer; it is not
restarted automatically. A network, API, audio or continuation-queue failure
stops bedtime mode and requests screen sleep rather than leaving an error screen.

Internet and Gemini quota are required for the entire session; 30 minutes is a
maximum timer duration, not a guarantee of uninterrupted generation. No offline
story library is implemented. Setup enables sliding-window context compression
and session resumption. During bedtime, a server goAway schedules a same-key
reconnect after the current segment's PCM and DMA have drained, using the latest
bounded resumption handle in PSRAM. The board deadline is preserved, not restarted;
the resumed synthetic turn cannot invoke settings or other side-effect tools.
Completed queued audio may finish after a planned server close. Missing handles,
rejected/expired handles, a close before the segment is fully received, or other
network errors still stop the story. This does not guarantee uninterrupted
30-120 minute operation and does not resume ordinary Chat or arbitrary outages.
Handles are never logged, written to flash, or reused across role/key changes.
Reference: [Google Live session management](https://ai.google.dev/gemini-api/docs/live-api/session-management).
It does not use the
short reminder TTS endpoint. No extra sound effects, pitch processing or EQ.

Host tests in `tools/test-voice.ps1` cover duration validation/defaults, segment
continuation, duplicate finish, no timer extension or side effects from synthetic
turns, deadline expiry, stale frames, wake/capture cancellation, explicit ending,
session reset, errors, allocation failure and full continuation queues. They do
not test real narration quality, device sound or actual 30-minute API operation.
