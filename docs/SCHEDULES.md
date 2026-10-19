# AI Schedules and Spoken Reminders

Online reminder speech uses the separate Reader key in Setting. Missing or failed
Reader access leaves the existing offline reminder fallback available. See
[key routing](GEMINI_KEYS.md).

## Use

Ask Hi Su to set a one-time reminder or alarm, read plans, move a plan, or cancel
one. Gemini uses `get_clock`, `set_schedule`, `list_schedule`, `delete_schedule`.
The model must read the actual device clock before interpreting relative dates,
clarify ambiguous times/targets, and never claim an unsupported recurring alarm
has been created. Datetimes are strict local `YYYY-MM-DDTHH:MM:SS`, converted
on-device using the configured fixed UTC offset. No DST or calendar account sync.

Menu > Calendar marks days containing events with `+`. Move between days with
up/down, press BOOT to view that day's chronological entries, up/down to scroll,
and BACK to return to the month or menu. Completed/claimed reminders remain
visible and count toward capacity until deleted by voice. Calendar and voice
tools use the same configured timezone.

## Storage and Clock

32 entries, each with up to 160 UTF-8 bytes, occupy about 6 KiB. Two work tables
live in PSRAM (about 12 KiB), not task stacks. No new task is created. Mutations
are immediately visible in RAM but are pending until `live_send` is idle and
the power reading is valid, with external power or more than 5% battery. Do not
reset before that idle save; the tool explicitly reports pending durability.
Schedules are independent of conversational memory and are never evicted by it.

Two 8 KiB flash slots at storage-relative 0x3e6000 and 0x3e8000 use CRC-checked
snapshots, alternating sequence numbers, header-last commit and readback. They
end at 0x3ea000, within the existing storage partition. Audio flashing ends at
0x3e6000, so installing audio preserves schedules, wallpaper and memory. No
filesystem or partition-table change. Flash is unencrypted: avoid sensitive text.

The clock must be synchronized after power loss (Wi-Fi SNTP or phone Setting).
No reminder can run while the board is off, and no powered-off auto-wake is
implemented. With an invalid clock nothing is delivered. Overdue entries within
24 hours are delivered when idle; older entries remain viewable but are not
automatically spoken. Past entries (claimed or missed) older than seven days
are removed in RAM during idle maintenance or creation of a new event, then
persisted with the usual A/B snapshot. Future events and the exact seven-day
boundary are retained. Cleanup requires a valid clock. Failed flush/claim saves
back off for five seconds; the idle poll no longer allocates unless an event is due.
Conversation, music and Setting postpone delivery. This
is a personal reminder feature, not a guaranteed safety-critical alarm clock.

## Speech and Fallback

When idle, `live_send` checks due entries once a second, attempts one short Leda
TTS request only if the last network state is online, then closes HTTP before
handing PSRAM PCM to `live_play`. The request has an eight-second async deadline
and two-second transport timeout, no automatic retry or model fallback. A new
wake cancels preparation. HTTP/quota/invalid/silent/too-long audio results fall
back to immutable Leda clip 5: a generic reminder with the event text on-screen.
Only the current event text is sent, never the list, memory or conversation.
The online model is configurable under menuconfig -> Hi Su voice; the default
is `gemini-2.5-flash-preview-tts`. The offline clip remains the fallback, not
another online model. Network checks use a small link snapshot rather than a
copy of the scan list. Stack minima are logged after reminder TTS and WSS cleanup,
with a warning below 1536 bytes. Stack remains 8192 bytes pending board measurements.

Prepare that fifth clip with `tools/prepare-battery-audio.py`; the first four
battery clips are reused. `idf.py reconfigure build` then full `idf.py flash`
installs all five clips; app-only flashing does not add the new recording.
The common audio image is 0x1e0000 bytes starting at absolute 0xa26000 for the
current partition layout. TTS calls consume the configured account quota; they
never enable billing. An unavailable TTS API still leaves offline playback.

The delivery claim is persisted before handing off audio, so a delayed online
result cannot cause a second offline/online notification. A reset after claiming
but before playback can skip audio; `delivered` means claimed, not confirmed
heard. The screen notification lasts 20 seconds; the entry remains in Calendar.
Mute, zero volume, invalid power and at/below 2% on battery suppress sound. A
new wake interrupts sound. There is no repeated ringing, snooze or physical
RTC wake in this version. PCM is chunked through the existing speaker DMA;
only one pending clip is held and is freed after playback.

## Verification

`tools/test-schedule.ps1` covers strict dates/UTF-8, CRUD, duplicates, capacity,
range queries, due/catch-up rules, claims across reboot, torn writes and changes
during flash commit. Voice tests cover tool validation, online/offline/error
selection, duplicate prevention, mute, Setting deferral and failed persistence.
Alert tests cover reminder TTS without charging, HTTP 429 and timeout cleanup.
Host audio tests load the generated image through the firmware's CRC validator.
Real-board acceptance is still required for timing, display layout and sound.

Set a reminder a few minutes ahead, end the conversation, inspect Calendar,
then test once online and once with Wi-Fi disabled. Reboot after an idle save
and sync the clock to verify persistence; do not erase flash to test upgrades.
