#pragma once
#include <stddef.h>
#include <stdbool.h>
typedef struct { size_t used; int offset, payload; unsigned opcode; bool active; } live_message_t;
/* -1 invalid, 0 incomplete, 1 complete. Capacity includes terminator. */
int live_message_feed(live_message_t *s, char *buffer, size_t capacity,
                      const char *bytes, int length, unsigned opcode, bool fin,
                      int offset, int payload);
