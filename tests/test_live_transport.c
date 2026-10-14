#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdio.h>
#include "live_transport_mock.h"
#undef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) ((ms) / 10)
#include "../components/hisu_voice/src/hisu_voice_live.c"

static input_t sent[32];
static output_t output[32];
static unsigned sent_count, output_count, events[7];
static uint32_t last_generation;
static bool full_input, interrupt_on_text;
static unsigned read_index;
static int peek_input(QueueHandle_t queue, void *data) {
    assert(queue == s_input);
    if (read_index == sent_count) return pdFALSE;
    *(input_t *)data = sent[read_index]; return pdTRUE;
}
static int receive_input(QueueHandle_t queue, void *data) {
    if (!peek_input(queue, data)) return pdFALSE;
    ++read_index; return pdTRUE;
}
int hisu_settings_get(hisu_setting_t key) { return 1; }
esp_err_t hisu_settings_get_gemini_key(char out[128]) { strcpy(out, "test-key"); return ESP_OK; }
esp_err_t hisu_network_set_performance(bool active) { return ESP_OK; }
static int save_queue(QueueHandle_t queue, const void *data) {
    if (queue == s_input) {
        if (full_input) return pdFALSE;
        assert(sent_count < 32); sent[sent_count++] = *(const input_t *)data;
    } else {
        assert(queue == s_output && output_count < 32);
        output[output_count++] = *(const output_t *)data;
    }
    return pdTRUE;
}
static void event(live_event_t kind, uint32_t generation, const char *text) {
    ++events[kind]; last_generation = generation;
    if (kind == LIVE_TEXT && interrupt_on_text) {
        interrupt_on_text = false;
        assert(voice_live_interrupt(generation + 1));
    }
}
static void reset(void) {
    for (unsigned i = 0; i < output_count; ++i) free(output[i].pcm);
    output_count = sent_count = 0; full_input = interrupt_on_text = false;
    read_index = 0; mock_queue_peek = peek_input; mock_queue_receive = receive_input;
    memset(events, 0, sizeof(events));
    s_input = (void *)1; s_output = (void *)2; s_callback = event;
    mock_queue_send = save_queue;
    s_queued_audio_bytes = 0;
    voice_live_open(10); s_ready = true;
}
static void receive(const char *wire) {
    connection_t connection = {.epoch = s_epoch, .message = (char *)wire};
    connection.parser.used = strlen(wire);
    parse_message(&connection);
}
static const char *audio_done = "{\"serverContent\":{\"outputTranscription\":{\"text\":\"reply\"},\"modelTurn\":{\"parts\":[{\"inlineData\":{\"mimeType\":\"audio/pcm;rate=24000\",\"data\":\"AAAAAA==\"}}]},\"turnComplete\":true}}";
int main(void) {
    int16_t stereo[512] = {0};
    s_speaker = (void *)1;
    assert(speaker_write(stereo,sizeof(stereo)));
    assert(mock_i2s_write_timeout == 100);
    mock_i2s_short_write = true;
    assert(!speaker_write(stereo,sizeof(stereo)));
    mock_i2s_short_write = false;
    s_speaker = NULL;
    assert(!speaker_write(stereo,sizeof(stereo)));
    char wire[SEND_BODY_CAPACITY], encoded[ENCODED_CAPACITY];
    memset(encoded, 'A', sizeof(encoded));
    format_audio(wire,encoded,sizeof(encoded)-1);
    cJSON *json = cJSON_Parse(wire);
    assert(json);
    cJSON *audio = cJSON_GetObjectItem(cJSON_GetObjectItem(json,"realtimeInput"),"audio");
    assert(strcmp(cJSON_GetObjectItem(audio,"mimeType")->valuestring,"audio/pcm;rate=16000") == 0);
    assert(strlen(cJSON_GetObjectItem(audio,"data")->valuestring) == sizeof(encoded)-1);
    cJSON_Delete(json);
    reset();
    input_t first = {.epoch=s_epoch,.generation=10,.count=640};
    for (unsigned i = 0; i < 640; ++i) first.pcm[i] = (int16_t)i;
    sent[0] = first; sent[1] = first; sent_count = 2;
    int16_t batch[SEND_SAMPLES];
    assert(collect_audio(&first,batch) == 1280 && read_index == 1);
    assert(memcmp(batch,first.pcm,sizeof(first.pcm)) == 0);
    assert(memcmp(batch+640,first.pcm,sizeof(first.pcm)) == 0);
    reset(); sent[0] = first; sent[0].kind = 2; sent_count = 1;
    assert(collect_audio(&first,batch) == 640 && read_index == 0);
    reset(); sent[0] = first; ++sent[0].generation; sent_count = 1;
    assert(collect_audio(&first,batch) == 640 && read_index == 0);
    reset(); sent[0] = first; ++sent[0].epoch; sent_count = 1;
    assert(collect_audio(&first,batch) == 640 && read_index == 0);
    reset(); first.count = 512; sent[0] = first; sent[1] = first; sent[2] = first; sent_count = 3;
    assert(collect_audio(&first,batch) == 1536 && read_index == 2);
    reset(); live_turn_start(&s_turn); live_turn_end(&s_turn);
    uint32_t epoch = s_epoch;
    assert(voice_live_interrupt(11) && s_epoch == epoch && voice_live_ready());
    assert(sent_count == 1 && sent[0].kind == 4 && sent[0].generation == 11);
    assert(!voice_live_begin(10) && s_generation == 11 && sent_count == 1);
    assert(!voice_live_begin(13) && s_generation == 11 && sent_count == 1);
    live_turn_start(&s_turn);
    assert(voice_live_begin(12)); assert(live_turn_start(&s_turn) == 0);
    live_turn_end(&s_turn);
    receive(audio_done); /* Canceled response arrives after the new input ends. */
    assert(events[LIVE_TEXT] == 0 && output_count == 0 && s_turn.obsolete_turns == 0);
    receive(audio_done);
    assert(events[LIVE_TEXT] == 1 && last_generation == 12);
    assert(output_count == 2 && output[0].generation == 12 && output[1].done);
    assert(s_epoch == epoch && !s_turn.response_pending);

    reset(); s_ready = false; epoch = s_epoch;
    assert(voice_live_interrupt(11) && s_epoch == epoch && sent_count == 0);
    receive("{\"setupComplete\":{}}");
    assert(voice_live_ready() && events[LIVE_READY] == 1 && last_generation == 11);

    reset(); full_input = true;
    assert(!voice_live_interrupt(11));
    assert(!s_failed && events[LIVE_ERROR] == 0); /* Caller owns the session lock: no synchronous error callback. */

    reset(); live_turn_start(&s_turn); live_turn_end(&s_turn);
    interrupt_on_text = true;
    receive(audio_done); /* Interruption races with parsing the final old response. */
    assert(s_generation == 11 && s_turn.obsolete_turns == 0);
    assert(output_count == 1 && !output[0].done && !current(output[0].epoch, output[0].generation));

    reset(); fail(s_epoch, 9);
    assert(events[LIVE_ERROR] == 1 && last_generation == 10);
    reset(); voice_live_close(); epoch = s_epoch;
    fail(epoch - 1, 10);
    assert(events[LIVE_ERROR] == 0);
    reset();
    puts("Live transport tests passed: same connection on interrupt, old-response fencing, setup interruption, queue failure and receive/control races. Hardware/TLS/API mocked.");
    return 0;
}
