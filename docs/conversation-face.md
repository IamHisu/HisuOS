# Conversation Face

Hisu shows animated cyan eyes on black while listening, connecting/thinking,
speaking or reporting a voice error. The idle wallpaper and music screen are
unchanged. The black face is intentional in both light and dark themes.

The 240x240 layout reserves y=166..231 for three lines of Vietnamese subtitles;
eyes remain above y=164. Long captions scroll to the latest text. UP/DOWN can
still scroll the subtitle area. Ending the conversation returns to the wallpaper.
After playback completes, the reply buffer and subtitle are cleared as the face
returns to listening. The `HISU_REPLY` monitor log is unaffected.

Automatic expressions follow voice state. Gemini can additionally use
`set_expression` with `auto`, `neutral`, `happy`, `sad`, `surprised`, `curious`,
`sleepy` or `focused`. These are simulated expressions, not human emotions.
The choice is ephemeral: listening, a new thinking turn, or idle resets it.
Expressions do not write settings/NVS.

Transcription and expression cues pass through the same bounded playback FIFO
as PCM. Receiving a network packet no longer immediately advances subtitles.
Text received before PCM waits for an audio block; late text is flushed at the
end of the played turn. Stale cues are discarded using the same epoch/generation checks as audio.
This provides segment-level alignment, not exact word alignment: the current
Live transcription stream carries no word timestamps, and device DMA adds a
small playback delay. A rolling UTF-8 caption window keeps long replies updating.

Animation uses the existing UI task and a 50 ms LVGL timer with two eye shapes and smile cutouts;
there is no new task, animation bitmap buffer or GIF decoder for the face.
Test geometry and transport fencing with `tools/test-ui.ps1` and
`tools/test-voice.ps1`. Host tests do not verify actual display or acoustic timing.
