#include "hisu_voice_session.h"
#include "hisu_voice_wake.h"
#include "hisu_voice_sse.h"
#include "hisu_voice_protocol.h"
#include "hisu_voice_live_turn.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned events;
static char last_event[128];
static bool accept_event(const char *data, size_t length, void *context)
{
    (void)context;
    assert(length < sizeof(last_event));
    memcpy(last_event, data, length);
    last_event[length] = 0;
    ++events;
    return true;
}

static bool reject_event(const char *data, size_t length, void *context)
{
    (void)data; (void)length; (void)context;
    return false;
}

int main(void)
{
    assert(!voice_wake_accept(1, 0.16f));
    assert(!voice_wake_accept(1, 0.1999f));
    assert(voice_wake_accept(1, 0.20f));
    assert(voice_wake_accept(1, 0.29f));
    assert(voice_wake_accept(1, 1.0f));
    assert(!voice_wake_accept(2, 0.90f));
    assert(!voice_wake_accept(1, -0.1f));
    assert(!voice_wake_accept(1, 1.1f));
    voice_session_t session = {0};
    voice_session_open(&session, 1000);
    uint32_t id = session.session_id;
    assert(session.state == VOICE_WAITING);
    assert(!voice_session_capture(&session, 1399));
    assert(voice_session_capture(&session, 1400));
    uint32_t old = session.generation;
    session.state = VOICE_PROCESSING;
    assert(!voice_session_expire(&session, 100000));
    session.state = VOICE_SPEAKING;
    voice_session_open(&session, 100000);
    assert(session.session_id == id);
    assert(!voice_session_current(&session, old));
    assert(!voice_session_finish(&session, old, 100001));
    assert(!voice_session_capture(&session, 100199));
    assert(voice_session_capture(&session, 100200));
    assert(voice_session_finish(&session, session.generation, 101000));
    assert(session.deadline_ms == 161300);
    assert(!voice_session_expire(&session, 161299));
    assert(voice_session_expire(&session, 161300));
    assert(session.state == VOICE_IDLE);
    voice_session_open(&session, 180000);
    assert(session.session_id != id);
    old = session.generation;
    voice_session_close(&session);
    assert(!voice_session_finish(&session, old, 180001));
    assert(!voice_session_current(&session, old));
    assert(voice_session_end_phrase("Thôi nhé!"));
    assert(voice_session_end_phrase("  thoi nhe. "));
    assert(voice_session_end_phrase("Tạm biệt."));
    assert(!voice_session_end_phrase("giải thích câu thôi nhé"));
    assert(!voice_session_end_phrase(""));
    assert(voice_session_end_phrase("Kết thúc cuộc trò chuyện."));
    assert(voice_session_end_phrase("kết thúc cuộc trò chuyện nhé"));
    assert(!voice_session_end_phrase("giải thích kết thúc cuộc trò chuyện nghĩa là gì"));
    voice_session_open(&session,200000);
    old=session.generation;
    assert(voice_session_finish_utterance(&session,old,201000,"kết thúc cuộc trò chuyện"));
    assert(session.state==VOICE_IDLE);
    assert(!voice_session_finish_utterance(&session,old,201001,""));
    assert(session.state==VOICE_IDLE);
    voice_session_open(&session,202000);
    assert(voice_session_finish_utterance(&session,session.generation,203000,"giải thích câu tạm biệt"));
    assert(session.state==VOICE_WAITING);

    live_turn_t turn;
    live_turn_reset(&turn);
    assert(!live_turn_accept_output(&turn));
    assert(live_turn_start(&turn) == LIVE_TURN_START);
    assert(live_turn_accept_input(&turn));
    live_turn_end(&turn);
    assert(live_turn_accept_output(&turn));
    live_turn_interrupt(&turn);
    assert(turn.obsolete_turns == 1 && !live_turn_accept_output(&turn));
    assert(live_turn_start(&turn) == LIVE_TURN_START);
    live_turn_end(&turn);
    assert(!live_turn_accept_output(&turn));
    assert(!live_turn_complete(&turn)); /* Old turn cannot finish the new response. */
    assert(live_turn_accept_output(&turn));
    assert(live_turn_complete(&turn));
    assert(!live_turn_accept_output(&turn));

    live_turn_reset(&turn);
    assert(live_turn_start(&turn) == LIVE_TURN_START);
    live_turn_interrupt(&turn); /* Interrupt while recording, before activityEnd. */
    assert(live_turn_start(&turn) == (LIVE_TURN_END_OLD | LIVE_TURN_START));
    live_turn_interrupt(&turn);
    assert(live_turn_start(&turn) == (LIVE_TURN_END_OLD | LIVE_TURN_START));
    assert(turn.obsolete_turns == 2);
    assert(!live_turn_complete(&turn));
    live_turn_end(&turn);
    assert(!live_turn_complete(&turn));
    assert(live_turn_accept_output(&turn));
    assert(live_turn_complete(&turn));

    live_turn_reset(&turn);
    live_turn_interrupt(&turn); /* Relisten while idle: no old response to await. */
    assert(live_turn_start(&turn) == LIVE_TURN_START);
    assert(live_turn_start(&turn) == 0); /* Speech begins inside the open input activity. */
    live_turn_end(&turn);
    assert(live_turn_accept_output(&turn) && live_turn_complete(&turn));

    char line[128], data[128];
    voice_sse_t parser;
    const char *wire = ": keepalive\r\nevent: step.delta\r\ndata: {\"text\":\"xin chào\"}\r\n\r\ndata: one\ndata: two\n\n";
    /* Every possible split, including inside UTF-8 and CRLF. */
    for (size_t split = 0; split <= strlen(wire); ++split) {
        events = 0;
        voice_sse_init(&parser, line, data, sizeof(data), accept_event, NULL);
        assert(voice_sse_feed(&parser, wire, split));
        assert(voice_sse_feed(&parser, wire + split, strlen(wire) - split));
        assert(voice_sse_complete(&parser));
        assert(events == 2 && strcmp(last_event, "one\ntwo") == 0);
    }
    events = 0;
    voice_sse_init(&parser, line, data, sizeof(data), accept_event, NULL);
    for (size_t i = 0; i < strlen(wire); ++i) assert(voice_sse_feed(&parser, wire + i, 1));
    assert(events == 2);
    voice_sse_init(&parser, line, data, sizeof(data), accept_event, NULL);
    assert(voice_sse_feed(&parser, "data: unfinished\n", 17));
    assert(!voice_sse_complete(&parser));
    voice_sse_init(&parser, line, data, 8, accept_event, NULL);
    assert(!voice_sse_feed(&parser, "data: too long\n\n", 16));
    assert(!voice_sse_complete(&parser));
    voice_sse_init(&parser, line, data, sizeof(data), reject_event, NULL);
    assert(!voice_sse_feed(&parser, "data: cancelled\n\n", 17));
    assert(!voice_sse_feed(&parser, "\n", 1));

    char reply_text[256] = "";
    voice_reply_t reply = { .text = reply_text };
    cJSON *json = cJSON_Parse("{\"candidates\":[{\"content\":{\"parts\":[{\"thought\":true,\"text\":\"hidden\"},{\"text\":\"Xin \"},{\"text\":\"chào\"}]}}]}");
    assert(json && voice_reply_append(&reply, json));
    assert(strcmp(reply_text, "Xin chào") == 0 && !reply.complete);
    cJSON_Delete(json);
    json = cJSON_Parse("{\"candidates\":[{\"finishReason\":\"STOP\"}]}");
    assert(voice_reply_append(&reply, json) && reply.complete);
    cJSON_Delete(json);
    json = cJSON_Parse("{\"candidates\":[{\"finishReason\":\"MAX_TOKENS\"}]}");
    assert(!voice_reply_append(&reply, json));
    cJSON_Delete(json);
    memset(reply_text, 'x', 255); reply_text[255] = 0;
    json = cJSON_Parse("{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"a\"}]}}]}");
    assert(!voice_reply_append(&reply, json));
    assert(strlen(reply_text) == 255);
    cJSON_Delete(json);
    const char *encoded = NULL;
    json = cJSON_Parse("{\"event_type\":\"step.delta\",\"delta\":{\"type\":\"audio\",\"data\":\"AAAA\",\"mime_type\":\"audio/l16\"}}");
    assert(voice_audio_event(json, &encoded) == VOICE_AUDIO_DATA && strcmp(encoded, "AAAA") == 0);
    cJSON_Delete(json);
    json = cJSON_Parse("{\"event_type\":\"interaction.completed\"}");
    assert(voice_audio_event(json, &encoded) == VOICE_AUDIO_DONE && encoded == NULL);
    cJSON_Delete(json);
    json = cJSON_Parse("{\"event_type\":\"interaction.failed\"}");
    assert(voice_audio_event(json, &encoded) == VOICE_AUDIO_ERROR);
    cJSON_Delete(json);
    json = cJSON_Parse("{\"event_type\":\"step.delta\",\"delta\":{\"type\":\"audio\",\"data\":\"AAAA\",\"mime_type\":\"audio/wav\"}}");
    assert(voice_audio_event(json, &encoded) == VOICE_AUDIO_ERROR);
    cJSON_Delete(json);
    json = cJSON_Parse("{\"event_type\":\"step.delta\",\"delta\":{\"type\":\"text\",\"text\":\"skip\"}}");
    assert(voice_audio_event(json, &encoded) == VOICE_AUDIO_IGNORE);
    cJSON_Delete(json);
    puts("Voice session, SSE framing and Gemini protocol tests passed.");
    return 0;
}
