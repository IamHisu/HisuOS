# Interpreter Mode

Interpreter mode is opt-in and off at every new Live session. Ask explicitly,
for example: "Bật phiên dịch Việt Anh". If the target is missing, the prompt asks
for it rather than choosing silently. `start_interpreter` accepts exactly one
language code: en, ja, ko, zh, fr, de, es, th, ru, it, pt, id, ar or hi. Vietnamese
is always the other side. Unsupported/extra/malformed arguments are rejected.

After the tool succeeds, Hi Su briefly confirms in Vietnamese. Subsequent
utterances are interpreted in the opposite direction within that pair, not
answered as questions or executed as commands. The mode explicitly overrides
the ordinary Vietnamese-only response/persona/short-answer rules. The prompt
preserves meaning, negation, names, numbers and register, and asks for repetition
instead of inventing unclear speech. Quality/direction recognition are model
behavior, not guaranteed by the firmware tests. Original Kore is unchanged.

This is turn-by-turn Gemini Live audio, not simultaneous interpretation or a
new STT/LLM/TTS chain. Existing capture limits (12 seconds maximum, about 700 ms
silence to finish) apply, so use short utterances and wait for the spoken result.
Internet and Gemini quota are required. No offline translation is implemented.
Interpreter mode reconnects using its own Translate key configured in Setting;
it never borrows Reader or Chat credentials. See [key routing](GEMINI_KEYS.md).

## Control and Lifetime

While active, tool calls are blocked except eye expressions. To stop or change
the pair, first wake Hi Su or press the normal voice-interrupt control, then say
"Tắt phiên dịch" or request the new pair. The interrupt arms one later captured
mode-control turn; a normal subsequent capture clears that permission. Even in
that control turn only start_interpreter, stop_interpreter, end_conversation and
expressions are allowed until interpretation is stopped. Translated requests to
change volume, play music, store facts, etc. cannot perform those actions.
The local goodbye-phrase shortcut is bypassed while interpreting, so translating
"tạm biệt" does not itself end the session.

The selected pair survives ordinary captures and interruption on the same
connection. It is not persisted: closing the session, normal 60-second listening
timeout, restart, or error followed by session close returns to ordinary mode.
Starting interpretation cancels a bedtime timer, pending music offer and pending
reboot confirmation. No extra task, audio queue or large buffer is created.
The listening subtitle shows the current pair as `Phiên dịch vi-en`, etc.

Active interpreter input/output is excluded from `hisu_memory_recent`; memory
tools are blocked. This is not a private/offline mode: audio goes to Gemini and
the existing reply monitor logs still contain output transcription. Content is
also available to the model inside the current Live session. Select the mode
first, then speak the text to translate; speech before activation remains an
ordinary conversation turn. Existing subtitle fonts are Vietnamese/Latin-focused
and do not guarantee rendering of Japanese, Chinese, Korean or other scripts;
the spoken output is independent of that font limitation.

## Verification

`tools/test-voice.ps1` tests language allowlisting, strict argument schemas,
default-off/reset, capture/interrupt persistence, mode-control gating, blocked
side effects and end requests, pair switching, normal-action restoration and
interaction with bedtime/music/reboot state. It uses mocked Gemini/audio APIs;
it does not establish translation accuracy, pronunciation or subtitle coverage.
