#include "hisu_voice_live_message.h"
static live_message_t parser;
static char buffer[64];
static char input[64];
char *test_input(void) { return input; }
char *test_output(void) { return buffer; }
void test_reset(void) { parser.used = 0; parser.offset = 0; parser.payload = 0; parser.opcode = 0; parser.active = false; }
int test_feed(int length, int opcode, int fin, int offset, int payload, int capacity)
{
    return live_message_feed(&parser, buffer, capacity, input, length, opcode, fin, offset, payload);
}
