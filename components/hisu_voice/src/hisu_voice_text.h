#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

typedef struct { char pending[12]; size_t used; } voice_text_filter_t;

/* Hold only prefixes of known non-speech markers across transcription chunks.
 * Unknown angle/braced text remains literal, including its UTF-8 bytes. */
static inline bool voice_text_filter(voice_text_filter_t *state, const char *text,
                                     char *out, size_t capacity)
{
    if (capacity <= strlen(text) + state->used) return false;
    static const char *const markers[] = {"<no speech>", "{pause}"};
    size_t written = 0;
    for (; *text; ++text) {
        state->pending[state->used++] = *text;
        while (state->used) {
            bool prefix = false, complete = false;
            for (unsigned m = 0; m < sizeof(markers)/sizeof(*markers); ++m) {
                size_t length = strlen(markers[m]), i = 0;
                if (state->used > length) continue;
                while (i < state->used && tolower((unsigned char)state->pending[i]) == markers[m][i]) ++i;
                if (i == state->used) { prefix = true; complete = state->used == length; break; }
            }
            if (complete) state->used = 0;
            if (prefix) break;
            out[written++] = state->pending[0];
            --state->used;
            memmove(state->pending, state->pending + 1, state->used);
        }
    }
    out[written] = 0;
    return true;
}
