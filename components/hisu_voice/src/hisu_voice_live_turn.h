#pragma once
#include <stdbool.h>

enum { LIVE_TURN_START = 1, LIVE_TURN_END_OLD = 2 };
typedef struct {
    bool input_open, response_pending, muted, restart_pending;
    unsigned obsolete_turns;
} live_turn_t;

void live_turn_reset(live_turn_t *turn);
void live_turn_interrupt(live_turn_t *turn);
unsigned live_turn_start(live_turn_t *turn);
void live_turn_end(live_turn_t *turn);
bool live_turn_accept_output(const live_turn_t *turn);
bool live_turn_accept_input(const live_turn_t *turn);
bool live_turn_complete(live_turn_t *turn);
