# Conversation Language

A user reported foreign-sounding speech after asking for music and confirmed
that the on-device subtitles were also in a foreign language. The excerpt showed
Gemini `First audio` but no music search/play tool or MP3 decoder log. This points
to a model-language problem, not merely a playback-rate problem. The excerpt does
not show the recognized input, exact response text, or establish why the model
switched languages.

The session instruction now starts in Vietnamese, stating that conversation and
tool-result explanations remain in Vietnamese unless the user explicitly requests
another language or a translation. Song/artist names, quotes, noisy recognition
and tool data are not instructions to switch languages. If input is unclear, the
model should ask for clarification in Vietnamese. Music requests use the music
tools, not synthesized singing, imitation or invented lyrics. Vietnamese speech is
prompted for a natural Southern Vietnamese accent across pronunciation, intonation
and rhythm, not only `r`; actual accent consistency is controlled by the model.
The default Leda voice
and 16 kHz input / 24 kHz output remain unchanged.

Silent-response continuation is also phrased in Vietnamese. There is no local
keyword/language detector, automatic tool execution, or rejection of names lacking
Vietnamese diacritics. Prompts reduce language drift but cannot guarantee model
compliance or correct recognition of every utterance.

Google's [Live capabilities guide](https://ai.google.dev/gemini-api/docs/live-api/capabilities)
states that native-audio models automatically choose language and do not support
explicit language-code configuration; language restrictions belong in system
instructions. No unsupported `speechConfig.languageCode` field is added.

Host tests verify valid setup/recovery JSON, Vietnamese-first instructions,
explicit-language/translation exceptions, music-tool requirements, preserved
voice/PCM settings and absence of an unsupported language code. Actual spoken
language and correct song selection still require a board/API test. For a repeat,
keep the exact user request, screenshot of subtitles and surrounding tool logs.

The monitor now logs accepted Gemini output-transcription segments at INFO under
`HISU_REPLY`, prefixed with `[connection_epoch:turn_generation]`. Each segment is
logged once on reception, without repeatedly printing the growing subtitle window
or waiting for audio playback. Thus the log may lead audible speech slightly and
can contain a segment later interrupted by the user. Stale/obsolete responses
rejected by the turn gate are not logged. No additional task, microphone transcript
log or credential log is introduced. These monitor lines contain conversation
content; review them before sharing. A reply without a transcription cannot be
reconstructed as text from PCM by this logger.
