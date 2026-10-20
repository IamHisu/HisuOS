#pragma once
#include <stdbool.h>
/* Called only by idle live_send, never on microphone/playback tasks. */
bool voice_memory_summary_poll(bool (*idle)(void));
