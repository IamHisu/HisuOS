#include "hisu_voice_session.h"
#include "hisu_voice_wake.h"
#include "hisu_voice_gate.h"
#include "hisu_voice_text.h"
#include "hisu_voice_sse.h"
#include "hisu_voice_protocol.h"
#include "hisu_voice_live_turn.h"
#include "hisu_voice_console.h"
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

static void gate_tests(void)
{
    voice_gate_t gate = {0};
    assert(voice_gate_threshold(&gate) == -32.0f);
    /* Continuous speech/noise on return from playback cannot open a turn. */
    for (unsigned i = 0; i < 100; ++i)
        assert(!voice_gate_start(&gate, true, -20.0f, 512));
    for (unsigned i = 0; i < 8; ++i) {
        voice_gate_observe(&gate, false, -50.0f, 512);
        assert(!voice_gate_start(&gate, false, -50.0f, 512));
    }
    assert(gate.armed && voice_gate_threshold(&gate) == -32.0f);
    /* Weak VAD chatter and isolated 256 ms bursts never start capture. */
    for (unsigned i = 0; i < 100; ++i)
        assert(!voice_gate_start(&gate, true, -35.0f, 512));
    for (unsigned repeat = 0; repeat < 5; ++repeat) {
        for (unsigned i = 0; i < 8; ++i)
            assert(!voice_gate_start(&gate, true, -25.0f, 512));
        assert(!voice_gate_start(&gate, false, -50.0f, 512));
    }
    for (unsigned i = 0; i < 9; ++i)
        assert(!voice_gate_start(&gate, true, -25.0f, 512));
    assert(voice_gate_start(&gate, true, -25.0f, 512));
    assert(voice_gate_continue(true, -38.0f, -32.0f));
    assert(!voice_gate_continue(true, -43.0f, -32.0f));
    assert(!voice_gate_continue(false, -20.0f, -32.0f));
    float noise = gate.noise_dbfs;
    voice_gate_observe(&gate, true, -20.0f, 16000);
    assert(gate.noise_dbfs == noise);
    voice_gate_pause(&gate);
    assert(!gate.armed && !gate.speech_samples && gate.noise_dbfs == noise);
    /* A noisy room raises the start threshold, but only to the bounded cap. */
    for (unsigned i = 0; i < 20; ++i)
        voice_gate_observe(&gate, false, -36.0f, 16000);
    assert(voice_gate_threshold(&gate) > -25.0f);
    for (unsigned i = 0; i < 8; ++i)
        assert(!voice_gate_start(&gate, false, -36.0f, 512));
    for (unsigned i = 0; i < 100; ++i)
        assert(!voice_gate_start(&gate, true, -30.0f, 512));
    for (unsigned i = 0; i < 9; ++i)
        assert(!voice_gate_start(&gate, true, -20.0f, 512));
    assert(voice_gate_start(&gate, true, -20.0f, 512));
    voice_gate_observe(&gate, false, -10.0f, 160000);
    assert(voice_gate_threshold(&gate) == -24.0f);
    noise = gate.noise_dbfs;
    voice_gate_observe(&gate, false, 1.0f, 512);
    assert(gate.noise_dbfs == noise);
    /* Detector chunk size does not change the required speech duration. */
    voice_gate_pause(&gate);
    for (unsigned i = 0; i < 25; ++i)
        assert(!voice_gate_start(&gate, false, -50.0f, 160));
    for (unsigned i = 0; i < 31; ++i)
        assert(!voice_gate_start(&gate, true, -20.0f, 160));
    assert(voice_gate_start(&gate, true, -20.0f, 160));
}

int main(void)
{
    const char *wire_text="<no speech>Rồi{pause}, rõ ràng.<NO SPEECH>";
    for (size_t split=0; split<=strlen(wire_text); ++split) {
        voice_text_filter_t filter={0}; char first[128], second[128], source[128];
        memcpy(source,wire_text,split); source[split]=0;
        assert(voice_text_filter(&filter,source,first,sizeof(first)));
        assert(voice_text_filter(&filter,wire_text+split,second,sizeof(second)));
        memcpy(first+strlen(first),second,strlen(second)+1);
        assert(!strcmp(first,"Rồi, rõ ràng.") && !filter.used);
    }
    voice_text_filter_t text_filter={0}; char filtered[128], assembled[128]="";
    for (const char *p=wire_text; *p; ++p) {
        char byte[2]={*p,0};
        assert(voice_text_filter(&text_filter,byte,filtered,sizeof(filtered)));
        memcpy(assembled+strlen(assembled),filtered,strlen(filtered)+1);
    }
    assert(!strcmp(assembled,"Rồi, rõ ràng.") && !text_filter.used);
    assert(voice_text_filter(&text_filter,"<no signal> {x} 1 < 2",filtered,sizeof(filtered)));
    assert(!strcmp(filtered,"<no signal> {x} 1 < 2"));
    assert(!voice_text_filter(&text_filter,"<no speech>",filtered,1) && !text_filter.used);
    gate_tests();
    voice_console_t console={0};
    const char *console_line="Mở bài \"Sóng Gió\"";
    for (size_t i=0;console_line[i];++i) assert(voice_console_feed(&console,(unsigned char)console_line[i])==VOICE_CONSOLE_NONE);
    assert(voice_console_feed(&console,'\r')==VOICE_CONSOLE_LINE && !strcmp(console.line,console_line));
    assert(voice_console_feed(&console,'\n')==VOICE_CONSOLE_NONE);
    assert(voice_console_feed(&console,'\n')==VOICE_CONSOLE_NONE);
    assert(voice_console_valid("Rồi, rõ ràng"));
    assert(!voice_console_valid("\xc0\xaf") && !voice_console_valid("\xed\xa0\x80"));
    assert(!voice_console_valid("\xf4\x90\x80\x80") && !voice_console_valid("\xe1\x80"));
    assert(!voice_console_valid("a\nb") && !voice_console_valid(""));
    for (unsigned i=0;i<255;++i) (void)voice_console_feed(&console,'a');
    assert(voice_console_feed(&console,'\n')==VOICE_CONSOLE_LINE && strlen(console.line)==255);
    for (unsigned i=0;i<256;++i) (void)voice_console_feed(&console,'b');
    (void)voice_console_feed(&console,127);
    assert(voice_console_feed(&console,'\n')==VOICE_CONSOLE_INVALID);
    (void)voice_console_feed(&console,0xe1); (void)voice_console_feed(&console,0xbb); (void)voice_console_feed(&console,0x9f);
    (void)voice_console_feed(&console,127); (void)voice_console_feed(&console,'x');
    assert(voice_console_feed(&console,'\n')==VOICE_CONSOLE_LINE && !strcmp(console.line,"x"));
    (void)voice_console_feed(&console,27); (void)voice_console_feed(&console,'x');
    assert(voice_console_feed(&console,'\n')==VOICE_CONSOLE_INVALID);
    /* Desktop reconnect discards a stale partial line, never submits it. */
    (void)voice_console_feed(&console,'x');
    assert(voice_console_feed(&console,0x15)==VOICE_CONSOLE_NONE);
    assert(voice_console_feed(&console,'\n')==VOICE_CONSOLE_INVALID);
    (void)voice_console_feed(&console,'y');
    assert(voice_console_feed(&console,'\n')==VOICE_CONSOLE_LINE && !strcmp(console.line,"y"));
    assert(!voice_wake_accept(1, 0.16f));
    for (const char *p="\x1e" "M0"; *p; ++p) (void)voice_console_feed(&console,(unsigned char)*p);
    assert(voice_console_feed(&console,'\r')==VOICE_CONSOLE_CONTROL && !strcmp(console.line,"M0"));
    assert(voice_console_feed(&console,'\n')==VOICE_CONSOLE_NONE);
    for (const char *p="M0"; *p; ++p) (void)voice_console_feed(&console,(unsigned char)*p);
    assert(voice_console_feed(&console,'\n')==VOICE_CONSOLE_LINE); /* Ordinary text stays text. */
    for (const char *p="stale\x1e" "M1"; *p; ++p) (void)voice_console_feed(&console,(unsigned char)*p);
    assert(voice_console_feed(&console,'\n')==VOICE_CONSOLE_INVALID);
    assert(!voice_console_valid("\x1e" "M0"));
    assert(voice_console_mic_muted(6000,1000) && voice_console_mic_muted(6000,5999));
    assert(!voice_console_mic_muted(6000,6000) && !voice_console_mic_muted(0,1000));
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
    assert(session.ready_ms == 101150 && session.deadline_ms == 161150);
    assert(!voice_session_capture(&session, 101149));
    voice_session_t capture_boundary=session;
    assert(voice_session_capture(&capture_boundary, 101150));
    assert(!voice_session_expire(&session, 161149));
    assert(voice_session_expire(&session, 161150));
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
