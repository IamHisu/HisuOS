#include "hisu_voice_live_turn.h"

void live_turn_reset(live_turn_t *s)
{
    *s = (live_turn_t){.muted = true};
}

void live_turn_interrupt(live_turn_t *s)
{
    if (s->response_pending) {
        ++s->obsolete_turns;
        s->response_pending = false;
    }
    s->muted = s->restart_pending = true;
}

unsigned live_turn_start(live_turn_t *s)
{
    unsigned action = !s->input_open || s->restart_pending ? LIVE_TURN_START : 0;
    if (s->input_open && s->restart_pending) {
        ++s->obsolete_turns;
        action |= LIVE_TURN_END_OLD;
    }
    s->input_open = s->muted = true;
    s->restart_pending = false;
    return action;
}

void live_turn_end(live_turn_t *s)
{
    s->input_open = s->muted = false;
    s->response_pending = true;
}

bool live_turn_accept_output(const live_turn_t *s)
{
    return s->response_pending && !s->muted && !s->obsolete_turns;
}

bool live_turn_accept_input(const live_turn_t *s)
{
    return !s->obsolete_turns && !s->restart_pending && (s->input_open || s->response_pending);
}

bool live_turn_complete(live_turn_t *s)
{
    /* Canceled turns end with turnComplete too. They cannot finish a new turn. */
    if (s->obsolete_turns) { --s->obsolete_turns; return false; }
    bool accept = live_turn_accept_output(s);
    s->response_pending = false;
    return accept;
}
