#include "hisu_voice_live.h"
#include "hisu_voice_live_message.h"
#include "hisu_voice_live_turn.h"
#include "hisu_voice_live_setup.h"
#include "hisu_voice_music.h"
#include "hisu_voice_caption.h"
#include "hisu_ui_face.h"
#include "hisu_network.h"
#include "hisu_settings.h"
#include "hisu_media.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "board_config.h"
#include "esp_websocket_client.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "mbedtls/base64.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MESSAGE_MAX (128 * 1024)
#define INPUT_SAMPLES 640
#define INPUT_QUEUE_FRAMES 256
#define SEND_SAMPLES 1600
#define ENCODED_CAPACITY (((SEND_SAMPLES * 2 + 2) / 3) * 4 + 1)
#define SEND_BODY_CAPACITY (ENCODED_CAPACITY + 128)
#define WSS_BUFFER_BYTES 8192
#define WSS_SEND_TIMEOUT_MS 5000
_Static_assert(SEND_BODY_CAPACITY <= WSS_BUFFER_BYTES, "Audio JSON must fit a single WebSocket frame");
typedef struct { uint32_t epoch, generation; unsigned kind; size_t count; int16_t pcm[INPUT_SAMPLES]; } input_t;
typedef struct { input_t frame, peek; char key[128], headers[160]; } send_workspace_t;
typedef struct {
    uint32_t epoch, generation;
    int16_t *pcm;
    size_t bytes;
    bool done, music;
    unsigned cue;
    int rate, channels;
} output_t;
static const char *TAG = "LIVE";
static void log_audio_heap(const char *phase)
{
    ESP_LOGI(TAG, "%s: internal free=%u largest=%u; DMA free=%u largest=%u; PSRAM free=%u",
             phase, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_epoch, s_generation;
static bool s_active, s_ready, s_failed;
static live_turn_t s_turn;
static struct {
    unsigned pending_tools;
    bool audio, deferred_complete, retry_used, retry_queued, retry_sent;
    size_t rx_bytes;
    int64_t rx_first_us, rx_last_us, max_rx_gap_us;
} s_reply_state;
static int64_t s_interrupt_deadline;
static QueueHandle_t s_input, s_output;
static StaticQueue_t s_input_control;
static uint8_t *s_input_storage;
static StaticQueue_t s_output_control;
static uint8_t *s_output_storage;
static char *s_send_storage;
static send_workspace_t *s_send_workspace;
/* Shared by music and Gemini on the sole I2S consumer, never by the producer. */
static int16_t *s_speaker_mix;
static TaskHandle_t s_transport_task, s_playback_task;
static live_event_fn s_callback;
static i2s_chan_handle_t s_speaker;
static int s_speaker_rate;
static bool s_speaker_enabled;
static int64_t s_end_us;
static size_t s_queued_audio_bytes;
/* Prepared by the transport worker; handed to the playback worker only after WSS teardown. */
static hisu_music_track_t *s_pending_music;
static bool s_music_playing;
static const hisu_music_track_t *s_music_output;
static bool s_music_started;
static bool s_music_needs_buffer, s_music_producer_done;
static esp_err_t s_music_result;
static uint32_t s_music_producer_ticket;
static size_t s_music_queued_bytes;
static uint64_t s_music_queued_us;
static void produce_music(hisu_music_track_t *track);
static bool s_reboot_pending;
static uint32_t s_reboot_generation;
static int64_t s_reboot_deadline;
static char s_confirmation[160];

/* Compare the entire user transcription, never a model-generated argument. */
static bool reboot_confirmed(void)
{
    const char *start = s_confirmation;
    while (*start && strchr(" \r\n\t", *start)) ++start;
    size_t length = strlen(start);
    while (length && strchr(" .!?\r\n\t", start[length - 1])) --length;
    static const char *const phrases[] = {"xác nhận khởi động lại", "Xác nhận khởi động lại",
        "xac nhan khoi dong lai", "Xac nhan khoi dong lai"};
    for (unsigned i = 0; i < sizeof(phrases) / sizeof(phrases[0]); ++i)
        if (strlen(phrases[i]) == length && !memcmp(start, phrases[i], length)) return true;
    return false;
}

static bool current(uint32_t epoch, uint32_t generation)
{
    portENTER_CRITICAL(&s_mux);
    bool ok = s_active && !s_failed && epoch == s_epoch && generation == s_generation;
    portEXIT_CRITICAL(&s_mux);
    return ok;
}
static void fail_with_message(uint32_t epoch, uint32_t generation, const char *message, bool exact_turn)
{
    portENTER_CRITICAL(&s_mux);
    bool notify = s_active && epoch == s_epoch && !s_failed && (!exact_turn || generation == s_generation);
    generation = s_generation;
    if (notify) { s_failed = true; s_ready = false; }
    portEXIT_CRITICAL(&s_mux);
    if (notify) s_callback(LIVE_ERROR, generation, message);
}
static void fail(uint32_t epoch, uint32_t generation)
{
    fail_with_message(epoch, generation, "Đã ngắt, gọi Hi Su để thử lại", false);
}
void voice_live_open(uint32_t generation)
{
    (void)hisu_music_cancel();
    portENTER_CRITICAL(&s_mux);
    ++s_epoch; s_generation = generation; s_active = true; s_ready = s_failed = false;
    s_reboot_pending = false; s_confirmation[0] = 0;
    live_turn_reset(&s_turn); s_interrupt_deadline = 0;
    memset(&s_reply_state, 0, sizeof(s_reply_state));
    portEXIT_CRITICAL(&s_mux);
}
void voice_live_close(void)
{
    portENTER_CRITICAL(&s_mux);
    ++s_epoch; s_active = s_ready = false;
    s_reboot_pending = false; s_confirmation[0] = 0;
    live_turn_reset(&s_turn); s_interrupt_deadline = 0;
    memset(&s_reply_state, 0, sizeof(s_reply_state));
    portEXIT_CRITICAL(&s_mux);
}
bool voice_live_ready(void)
{
    portENTER_CRITICAL(&s_mux);
    bool ready = s_active && s_ready && !s_failed;
    portEXIT_CRITICAL(&s_mux);
    return ready;
}
static bool enqueue(uint32_t generation, unsigned kind, const int16_t *pcm, size_t count)
{
    input_t frame = {.generation = generation, .kind = kind, .count = count};
    portENTER_CRITICAL(&s_mux);
    frame.epoch = s_epoch;
    bool ready = s_ready && s_active && !s_failed && generation == s_generation;
    portEXIT_CRITICAL(&s_mux);
    if (!ready || count > INPUT_SAMPLES) return false;
    if (count) memcpy(frame.pcm, pcm, count * 2);
    if (xQueueSend(s_input, &frame, 0) == pdTRUE) return true;
    ESP_LOGW(TAG, "Input queue full; abort turn instead of losing audio");
    fail(frame.epoch, generation);
    return false;
}
bool voice_live_interrupt(uint32_t generation)
{
    input_t frame = {.generation = generation, .kind = 4};
    portENTER_CRITICAL(&s_mux);
    bool active = s_active && !s_failed;
    bool ready = s_ready;
    if (active) {
        frame.epoch = s_epoch;
        s_generation = generation;
        /* A queued continuation has not opened another server turn yet. */
        if (s_reply_state.retry_queued) s_turn.response_pending = false;
        live_turn_interrupt(&s_turn);
        memset(&s_reply_state, 0, sizeof(s_reply_state));
        if (s_turn.obsolete_turns || s_turn.input_open)
            s_interrupt_deadline = esp_timer_get_time() + 10000000;
    }
    portEXIT_CRITICAL(&s_mux);
    /* During setup, retain the connection and deliver LIVE_READY to this generation. */
    return active && (!ready || xQueueSend(s_input, &frame, 0) == pdTRUE);
}
bool voice_live_begin(uint32_t generation)
{
    portENTER_CRITICAL(&s_mux);
    bool ready = s_active && s_ready && !s_failed &&
        (generation == s_generation || generation == s_generation + 1);
    if (ready) {
        s_generation = generation; s_confirmation[0] = 0;
        memset(&s_reply_state, 0, sizeof(s_reply_state));
    }
    portEXIT_CRITICAL(&s_mux);
    return ready && enqueue(generation, 1, NULL, 0);
}
bool voice_live_feed(uint32_t generation, const int16_t *pcm, size_t samples)
{
    while (samples) {
        size_t count = samples > INPUT_SAMPLES ? INPUT_SAMPLES : samples;
        if (!enqueue(generation, 0, pcm, count)) return false;
        pcm += count; samples -= count;
    }
    return true;
}
bool voice_live_end(uint32_t generation) { return enqueue(generation, 2, NULL, 0); }

typedef struct {
    uint32_t epoch;
    char *message;
    live_message_t parser;
    bool connected;
    esp_transport_handle_t tls, ws;
} connection_t;

static void destroy_connection_transport(connection_t *connection)
{
    /* The client does not own ext_transport; destroy the wrapper before its TLS parent. */
    if (connection->ws) { esp_transport_destroy(connection->ws); connection->ws = NULL; }
    if (connection->tls) { esp_transport_destroy(connection->tls); connection->tls = NULL; }
}

static bool create_connection_transport(connection_t *connection, const char *headers)
{
    connection->tls = esp_transport_ssl_init();
    if (!connection->tls) return false;
    esp_transport_ssl_crt_bundle_attach(connection->tls, esp_crt_bundle_attach);
    connection->ws = esp_transport_ws_init(connection->tls);
    if (!connection->ws) { destroy_connection_transport(connection); return false; }
    esp_transport_set_default_port(connection->ws, 443);
    const esp_transport_ws_config_t config = {
        .ws_path = "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent",
        .headers = headers, .propagate_control_frames = true,
    };
    if (esp_transport_ws_set_config(connection->ws, &config) != ESP_OK) {
        destroy_connection_transport(connection); return false;
    }
    return true;
}

static bool queue_cue(uint32_t epoch, uint32_t generation, unsigned kind, const char *text)
{
    char *copy = heap_caps_malloc(256, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) return false;
    copy[0] = 0;
    voice_caption_append(copy, 256, text);
    output_t cue = {.epoch = epoch, .generation = generation, .pcm = (int16_t *)copy, .cue = kind};
    if (xQueueSend(s_output, &cue, 0) == pdTRUE) return true;
    free(copy); return false;
}

static void play_cue(const output_t *cue)
{
    if (current(cue->epoch, cue->generation))
        s_callback(cue->cue == 1 ? LIVE_TEXT : LIVE_EXPRESSION, cue->generation, (const char *)cue->pcm);
}

typedef struct { uint32_t epoch, generation; char text[256]; } playback_caption_t;

static void hold_caption(playback_caption_t *caption, const output_t *cue)
{
    if (!current(cue->epoch, cue->generation)) return;
    if (caption->epoch != cue->epoch || caption->generation != cue->generation) caption->text[0] = 0;
    caption->epoch = cue->epoch; caption->generation = cue->generation;
    voice_caption_append(caption->text, sizeof(caption->text), (const char *)cue->pcm);
}

static void flush_caption(playback_caption_t *caption, uint32_t epoch, uint32_t generation)
{
    if (caption->text[0] && caption->epoch == epoch && caption->generation == generation &&
        current(epoch, generation)) s_callback(LIVE_TEXT, generation, caption->text);
    caption->text[0] = 0;
}

static void complete_reply(uint32_t epoch, uint32_t generation)
{
    bool done = false, retry = false, silent = false;
    portENTER_CRITICAL(&s_mux);
    if (epoch == s_epoch && generation == s_generation && s_active && !s_failed) {
        if (s_turn.obsolete_turns) {
            (void)live_turn_complete(&s_turn);
        } else if (live_turn_accept_output(&s_turn) && !s_reply_state.retry_queued) {
            if (s_reply_state.pending_tools) s_reply_state.deferred_complete = true;
            else {
                (void)live_turn_complete(&s_turn);
                s_reply_state.deferred_complete = false;
                if (s_reply_state.audio) done = true;
                else if (!s_reply_state.retry_used) {
                    s_reply_state.retry_used = s_reply_state.retry_queued = retry = true;
                    /* Accept an immediate tool follow-up before the queued retry runs. */
                    live_turn_end(&s_turn);
                } else silent = true;
            }
        }
        if (!s_turn.obsolete_turns) s_interrupt_deadline = 0;
    }
    portEXIT_CRITICAL(&s_mux);
    if (done) {
        output_t completed = {.epoch=epoch, .generation=generation, .done=true};
        if (xQueueSend(s_output, &completed, 0) != pdTRUE) fail(epoch, generation);
    } else if (retry) {
        ESP_LOGW(TAG,"Reply ended without audio; requesting spoken continuation once");
        input_t *continuation = heap_caps_calloc(1,sizeof(*continuation),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (continuation) {
            continuation->epoch=epoch; continuation->generation=generation; continuation->kind=7;
        }
        bool queued = continuation && xQueueSend(s_input, continuation, 0) == pdTRUE;
        free(continuation);
        if (!queued) fail(epoch, generation);
    } else if (silent) {
        ESP_LOGW(TAG,"No audio after spoken continuation; closing conversation quietly");
        fail_with_message(epoch, generation, "", true);
    }
}

static void parse_message(connection_t *connection)
{
    portENTER_CRITICAL(&s_mux);
    uint32_t generation = s_generation;
    portEXIT_CRITICAL(&s_mux);
    if (!current(connection->epoch, generation)) return;
    cJSON *json = cJSON_ParseWithLength(connection->message, connection->parser.used);
    if (!json) { fail(connection->epoch, generation); return; }
    if (cJSON_GetObjectItem(json, "error") || cJSON_GetObjectItem(json, "goAway")) {
        cJSON *code = cJSON_GetObjectItem(cJSON_GetObjectItem(json, "error"), "code");
        ESP_LOGW(TAG, "Server error code=%d or session expiry", cJSON_IsNumber(code) ? code->valueint : 0);
        fail(connection->epoch, generation);
    } else if (cJSON_GetObjectItem(json, "setupComplete")) {
        portENTER_CRITICAL(&s_mux);
        bool first_setup = connection->epoch == s_epoch && s_active && !s_failed && !s_ready;
        if (first_setup) s_ready = true;
        portEXIT_CRITICAL(&s_mux);
        if (!first_setup) { cJSON_Delete(json); return; }
        ESP_LOGI(TAG, "Gemini Live setup ready");
        ESP_LOGI(TAG, "RAM internal free=%u min=%u largest=%u; PSRAM free=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        s_callback(LIVE_READY, generation, "");
        input_t wake = {.epoch = connection->epoch, .generation = generation, .kind = 6};
        if (xQueueSend(s_input, &wake, 0) != pdTRUE) fail(connection->epoch, generation);
    } else if (cJSON_GetObjectItem(json, "toolCall")) {
        portENTER_CRITICAL(&s_mux);
        bool accept = live_turn_accept_output(&s_turn);
        bool recovery = s_reply_state.retry_sent;
        portEXIT_CRITICAL(&s_mux);
        if (!accept) { cJSON_Delete(json); return; }
        if (recovery) {
            ESP_LOGW(TAG,"Spoken continuation returned tools without audio; closing conversation quietly");
            fail_with_message(connection->epoch,generation,"",true);
            cJSON_Delete(json); return;
        }
        cJSON *calls = cJSON_GetObjectItem(cJSON_GetObjectItem(json, "toolCall"), "functionCalls");
        cJSON *call;
        cJSON_ArrayForEach(call, calls) {
            cJSON *name = cJSON_GetObjectItem(call, "name"), *id = cJSON_GetObjectItem(call, "id");
            if (!cJSON_IsString(name) || !cJSON_IsString(id)) {
                fail(connection->epoch, generation); break;
            }
            /* NVS writes run on the transport worker, never the WebSocket callback. */
            char *body = cJSON_PrintUnformatted(call);
            input_t frame = {.epoch = connection->epoch, .generation = generation, .kind = 5};
            if (!body || strlen(body) >= sizeof(frame.pcm)) {
                cJSON_free(body); fail(connection->epoch, generation); break;
            }
            strcpy((char *)frame.pcm, body); cJSON_free(body);
            portENTER_CRITICAL(&s_mux);
            bool still_current = connection->epoch == s_epoch && generation == s_generation;
            if (still_current) {
                ++s_reply_state.pending_tools;
                s_reply_state.deferred_complete = false;
                if (s_reply_state.retry_queued) s_reply_state.retry_used = false;
                s_reply_state.retry_queued = false;
            }
            portEXIT_CRITICAL(&s_mux);
            if (!still_current) break;
            if (xQueueSend(s_input, &frame, 0) != pdTRUE) { fail(connection->epoch, generation); break; }
        }
    } else {
        cJSON *content = cJSON_GetObjectItem(json, "serverContent");
        portENTER_CRITICAL(&s_mux);
        bool accept_input = live_turn_accept_input(&s_turn);
        bool accept_output = live_turn_accept_output(&s_turn);
        bool interrupted = cJSON_IsTrue(cJSON_GetObjectItem(content, "interrupted"));
        if (interrupted) accept_input = accept_output = false;
        portEXIT_CRITICAL(&s_mux);
        cJSON *input = cJSON_GetObjectItem(cJSON_GetObjectItem(content, "inputTranscription"), "text");
        if (accept_input && cJSON_IsString(input)) {
            portENTER_CRITICAL(&s_mux);
            if (connection->epoch == s_epoch && generation == s_generation) {
                size_t used = strlen(s_confirmation), extra = strlen(input->valuestring);
                if (extra >= sizeof(s_confirmation) - used) {
                    s_reboot_pending = false; /* Truncation must never create a confirmation. */
                } else memcpy(s_confirmation + used, input->valuestring, extra + 1);
            }
            portEXIT_CRITICAL(&s_mux);
            s_callback(LIVE_INPUT, generation, input->valuestring);
        }
        cJSON *text = cJSON_GetObjectItem(cJSON_GetObjectItem(content, "outputTranscription"), "text");
        /* Deliver text on the playback worker, ordered with PCM, not on network arrival. */
        if (accept_output && cJSON_IsString(text) && text->valuestring[0]) {
            /* Log each received segment once, outside the microphone/session lock. */
            ESP_LOGI("HISU_REPLY", "[%u:%u] %s", (unsigned)connection->epoch,
                     (unsigned)generation, text->valuestring);
            if (!queue_cue(connection->epoch, generation, 1, text->valuestring)) {
                fail(connection->epoch, generation); cJSON_Delete(json); return;
            }
        }
        cJSON *parts = cJSON_GetObjectItem(cJSON_GetObjectItem(content, "modelTurn"), "parts");
        cJSON *part;
        cJSON_ArrayForEach(part, parts) {
            if (!accept_output) break;
            cJSON *blob = cJSON_GetObjectItem(part, "inlineData");
            cJSON *encoded = cJSON_GetObjectItem(blob, "data");
            cJSON *mime = cJSON_GetObjectItem(blob, "mimeType");
            if (!cJSON_IsString(encoded)) continue;
            if (!cJSON_IsString(mime) || strcmp(mime->valuestring, "audio/pcm;rate=24000")) {
                fail(connection->epoch, generation); break;
            }
            size_t n = strlen(encoded->valuestring), bytes = 0;
            int16_t *pcm = heap_caps_malloc(n * 3 / 4 + 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!pcm) { fail(connection->epoch, generation); break; }
            int err = mbedtls_base64_decode((unsigned char *)pcm, n * 3 / 4 + 4, &bytes,
                                            (unsigned char *)encoded->valuestring, n);
            output_t audio = {.epoch = connection->epoch, .generation = generation, .pcm = pcm, .bytes = bytes};
            portENTER_CRITICAL(&s_mux);
            bool room = s_queued_audio_bytes + bytes <= 512 * 1024;
            if (room) s_queued_audio_bytes += bytes;
            portEXIT_CRITICAL(&s_mux);
            if (err || bytes % 2 || !room || xQueueSend(s_output, &audio, 0) != pdTRUE) {
                if (room) { portENTER_CRITICAL(&s_mux); s_queued_audio_bytes -= bytes; portEXIT_CRITICAL(&s_mux); }
                free(pcm); fail(connection->epoch, generation); break;
            }
            if (bytes) {
                int64_t received_us=esp_timer_get_time();
                portENTER_CRITICAL(&s_mux);
                if (connection->epoch == s_epoch && generation == s_generation) {
                    if (!s_reply_state.rx_bytes) s_reply_state.rx_first_us=received_us;
                    else if (received_us-s_reply_state.rx_last_us>s_reply_state.max_rx_gap_us)
                        s_reply_state.max_rx_gap_us=received_us-s_reply_state.rx_last_us;
                    s_reply_state.rx_bytes+=bytes; s_reply_state.rx_last_us=received_us;
                    s_reply_state.audio = true;
                    s_reply_state.retry_queued = s_reply_state.deferred_complete = false;
                }
                portEXIT_CRITICAL(&s_mux);
            }
        }
        if (cJSON_IsTrue(cJSON_GetObjectItem(content, "turnComplete"))) {
            cJSON *status = cJSON_GetObjectItem(content, "interactionStatus");
            if (!status) status = cJSON_GetObjectItem(json, "interactionStatus");
            bool in_progress = cJSON_IsString(status) && !strcmp(status->valuestring,"IN_PROGRESS");
            if (!in_progress && !interrupted) complete_reply(connection->epoch, generation);
            else if (interrupted) {
                portENTER_CRITICAL(&s_mux);
                if (connection->epoch == s_epoch && s_turn.obsolete_turns) (void)live_turn_complete(&s_turn);
                portEXIT_CRITICAL(&s_mux);
            }
        }
    }
    cJSON_Delete(json);
}
static void websocket_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    connection_t *connection = arg;
    esp_websocket_event_data_t *event = data;
    if (id == WEBSOCKET_EVENT_CONNECTED) {
        int socket = connection->tls ? esp_transport_get_socket(connection->tls) : -1;
        int enabled = 1;
        if (socket >= 0 && setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) == 0)
            ESP_LOGI(TAG, "WSS low-latency TCP enabled");
        else ESP_LOGW(TAG, "Cannot enable WSS TCP_NODELAY");
        portENTER_CRITICAL(&s_mux); connection->connected = true; portEXIT_CRITICAL(&s_mux);
    }
    else if (id == WEBSOCKET_EVENT_ERROR || id == WEBSOCKET_EVENT_DISCONNECTED) {
        if (id == WEBSOCKET_EVENT_ERROR && event) {
            ESP_LOGW(TAG, "WSS error: handshake=%d TLS=0x%x",
                     event->error_handle.esp_ws_handshake_status_code,
                     (unsigned)event->error_handle.esp_tls_stack_err);
            log_audio_heap("WSS failure RAM");
        }
        portENTER_CRITICAL(&s_mux); uint32_t generation = s_generation; portEXIT_CRITICAL(&s_mux);
        fail(connection->epoch, generation);
    } else if (id == WEBSOCKET_EVENT_DATA && (event->op_code == 1 || event->op_code == 2 || event->op_code == 0)) {
        int result = live_message_feed(&connection->parser, connection->message, MESSAGE_MAX + 1,
                                      event->data_ptr, event->data_len, event->op_code, event->fin,
                                      event->payload_offset, event->payload_len);
        if (result < 0) {
            portENTER_CRITICAL(&s_mux); uint32_t generation = s_generation; portEXIT_CRITICAL(&s_mux);
            fail(connection->epoch, generation); return;
        }
        if (result == 1) parse_message(connection);
    } else if (id == WEBSOCKET_EVENT_DATA && event->op_code == 8) {
        portENTER_CRITICAL(&s_mux); uint32_t generation = s_generation; portEXIT_CRITICAL(&s_mux);
        fail(connection->epoch, generation);
    }
}
static bool send_text(esp_websocket_client_handle_t client, const char *text)
{
    size_t length = strlen(text);
    int64_t started = esp_timer_get_time();
    int sent = esp_websocket_client_send_text(client, text, length, pdMS_TO_TICKS(WSS_SEND_TIMEOUT_MS));
    int64_t elapsed = (esp_timer_get_time() - started) / 1000;
    if (elapsed > 100) ESP_LOGW(TAG, "Slow WSS send: %lld ms, bytes=%u, pending=%u", (long long)elapsed,
                                (unsigned)length, (unsigned)uxQueueMessagesWaiting(s_input));
    return sent == (int)length;
}
static bool send_wake(esp_websocket_client_handle_t client, const input_t *frame)
{
    portENTER_CRITICAL(&s_mux);
    bool send = s_active && s_ready && !s_failed && frame->epoch == s_epoch &&
        frame->generation == s_generation && !s_turn.response_pending && !s_turn.input_open &&
        !s_turn.obsolete_turns;
    if (send) {
        /* A text turn uses the same output fencing and playback path as microphone turns. */
        live_turn_reset(&s_turn);
        live_turn_end(&s_turn);
        s_interrupt_deadline = 0;
        s_end_us = esp_timer_get_time();
    }
    portEXIT_CRITICAL(&s_mux);
    if (!send) return true;
    ESP_LOGI(TAG, "Sending wake phrase to Gemini");
    return send_text(client, "{\"clientContent\":{\"turns\":[{\"role\":\"user\","
        "\"parts\":[{\"text\":\"Hi Su\"}]}],\"turnComplete\":true}}");
}
static bool send_spoken_continuation(esp_websocket_client_handle_t client, const input_t *frame)
{
    portENTER_CRITICAL(&s_mux);
    bool send = s_active && !s_failed && frame->epoch == s_epoch && frame->generation == s_generation &&
        s_reply_state.retry_queued && !s_reply_state.audio && live_turn_accept_output(&s_turn);
    if (send) {
        s_reply_state.retry_queued = false; s_reply_state.retry_sent = true;
        s_end_us = esp_timer_get_time();
    }
    portEXIT_CRITICAL(&s_mux);
    if (!send) return true;
    return send_text(client,"{\"clientContent\":{\"turns\":[{\"role\":\"user\",\"parts\":[{\"text\":\""
        "Hãy nói tiếp câu trả lời trước bằng tiếng Việt, ngắn gọn và tự nhiên. Không gọi công cụ, "
        "không lặp lại thao tác đã thực hiện; dùng kết quả công cụ đã có. Nếu mình chỉ gọi Hi Su, "
        "hãy đáp lại thân thiện bằng một câu tiếng Việt ngắn rồi chờ mình nói tiếp.\"}]}],\"turnComplete\":true}}");
}

static size_t collect_audio(const input_t *first, int16_t *pcm, input_t *next)
{
    size_t samples = first->count;
    memcpy(pcm, first->pcm, samples * sizeof(*pcm));
    /* Drain only queued audio from this turn, leaving control messages in order. */
    while (xQueuePeek(s_input, next, 0) == pdTRUE && next->kind == 0 &&
           next->epoch == first->epoch && next->generation == first->generation &&
           next->count <= SEND_SAMPLES - samples) {
        if (xQueueReceive(s_input, next, 0) != pdTRUE) break;
        memcpy(pcm + samples, next->pcm, next->count * sizeof(*pcm));
        samples += next->count;
    }
    return samples;
}
static void format_audio(char *body, const char *encoded, size_t length)
{
    static const char prefix[] = "{\"realtimeInput\":{\"audio\":{\"mimeType\":\"audio/pcm;rate=16000\",\"data\":\"";
    static const char suffix[] = "\"}}}";
    _Static_assert(sizeof(prefix) - 1 + ENCODED_CAPACITY - 1 + sizeof(suffix) <= SEND_BODY_CAPACITY,
                   "Audio JSON must fit the send buffer");
    memcpy(body, prefix, sizeof(prefix) - 1);
    memcpy(body + sizeof(prefix) - 1, encoded, length);
    memcpy(body + sizeof(prefix) - 1 + length, suffix, sizeof(suffix));
}
static bool tool_integer(const cJSON *args, const char *key, int minimum, int maximum, int *value)
{
    const cJSON *number = cJSON_GetObjectItemCaseSensitive(args, key);
    if (!cJSON_IsObject(args) || cJSON_GetArraySize(args) != 1 || !cJSON_IsNumber(number) ||
        number->valuedouble < minimum || number->valuedouble > maximum ||
        number->valuedouble != number->valueint) return false;
    *value = number->valueint;
    return true;
}

static char *execute_tool(const input_t *frame, bool *end_session)
{
    *end_session = false;
    if (!current(frame->epoch, frame->generation)) return NULL;
    cJSON *call = cJSON_Parse((const char *)frame->pcm);
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(call, "name");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(call, "id");
    const cJSON *args = cJSON_GetObjectItemCaseSensitive(call, "args");
    if (!cJSON_IsString(name) || !cJSON_IsString(id)) { cJSON_Delete(call); return NULL; }
    cJSON *response = cJSON_CreateObject();
    cJSON *tool = cJSON_AddObjectToObject(response, "toolResponse");
    cJSON *results = cJSON_AddArrayToObject(tool, "functionResponses");
    cJSON *result = cJSON_CreateObject();
    if (!response || !tool || !results || !result || !cJSON_AddItemToArray(results, result)) {
        cJSON_Delete(result); cJSON_Delete(response); cJSON_Delete(call); return NULL;
    }
    cJSON *value = cJSON_AddObjectToObject(result, "response");
    if (!value || !cJSON_AddStringToObject(result,"id",id->valuestring) ||
        !cJSON_AddStringToObject(result,"name",name->valuestring)) {
        cJSON_Delete(response); cJSON_Delete(call); return NULL;
    }
    bool no_args = !args || (cJSON_IsObject(args) && cJSON_GetArraySize(args) == 0);
    bool valid = false, changed = false;
    int setting_value = 0;
    hisu_setting_t setting = HISU_SETTING_COUNT;
    esp_err_t err = ESP_OK;
    if (!strcmp(name->valuestring,"search_music")) {
        const cJSON *query = cJSON_GetObjectItemCaseSensitive(args,"query");
        valid = cJSON_IsObject(args) && cJSON_GetArraySize(args) == 1 && cJSON_IsString(query) &&
            query->valuestring[0] && strlen(query->valuestring) <= 180;
        if (valid) {
            cJSON *songs = cJSON_AddArrayToObject(value,"songs");
            err = songs ? hisu_music_search(query->valuestring,songs) : ESP_ERR_NO_MEM;
        }
    } else if (!strcmp(name->valuestring,"play_music")) {
        const cJSON *song = cJSON_GetObjectItemCaseSensitive(args,"id");
        valid = cJSON_IsObject(args) && cJSON_GetArraySize(args) == 1 && cJSON_IsString(song) &&
            hisu_music_valid_id(song->valuestring);
        if (valid) {
            free(s_pending_music); s_pending_music = NULL;
            hisu_music_track_t *track = heap_caps_calloc(1,sizeof(*track),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            err = track ? hisu_music_queue(song->valuestring,track) : ESP_ERR_NO_MEM;
            if (err == ESP_OK && current(frame->epoch,frame->generation)) {
                s_pending_music=track;
                *end_session=true;
                cJSON_AddStringToObject(value,"status","queued");
            } else {
                if (track) hisu_music_finish(track->ticket);
                free(track);
                if (err == ESP_OK) err=ESP_ERR_INVALID_STATE;
            }
        }
    } else if (!strcmp(name->valuestring,"stop_music") && no_args) {
        valid=true; (void)hisu_music_cancel();
    } else if (!strcmp(name->valuestring,"get_network_status") && no_args) {
        hisu_network_status_t network;
        hisu_network_get(&network);
        static const char *const states[] = {"unknown", "checking", "online", "captive_portal", "offline"};
        valid = true;
        cJSON_AddBoolToObject(value,"enabled",network.enabled);
        cJSON_AddBoolToObject(value,"connected",network.connected);
        cJSON_AddBoolToObject(value,"joining",network.joining);
        cJSON_AddStringToObject(value,"ssid",network.connected ? network.ssid : "");
        cJSON_AddStringToObject(value,"ip",network.connected ? network.ip : "");
        if (network.connected) cJSON_AddNumberToObject(value,"rssi_dbm",network.rssi);
        cJSON_AddStringToObject(value,"internet",!network.connected ? "disconnected" :
            network.internet >= HISU_INTERNET_UNKNOWN && network.internet <= HISU_INTERNET_OFFLINE ?
            states[network.internet] : "unknown");
        cJSON_AddStringToObject(value,"note","Last observed state; connectivity probes pause during voice calls");
    } else if (!strcmp(name->valuestring,"get_system_info") && no_args) {
        const esp_app_desc_t *app = esp_app_get_description();
        valid = true;
        cJSON_AddStringToObject(value,"firmware",app->version);
        cJSON_AddStringToObject(value,"project",app->project_name);
        cJSON_AddStringToObject(value,"idf",app->idf_ver);
        cJSON_AddNumberToObject(value,"uptime_seconds",(double)(esp_timer_get_time() / 1000000));
        cJSON_AddNumberToObject(value,"internal_free_bytes",(double)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        cJSON_AddNumberToObject(value,"internal_largest_bytes",(double)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        cJSON_AddNumberToObject(value,"psram_free_bytes",(double)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        cJSON_AddNumberToObject(value,"screen_width",HISU_MEDIA_WIDTH);
        cJSON_AddNumberToObject(value,"screen_height",HISU_MEDIA_HEIGHT);
    } else if (!strcmp(name->valuestring,"get_wallpaper") && no_args) {
        hisu_media_status_t media;
        hisu_media_get(&media);
        valid = true;
        cJSON_AddBoolToObject(value,"uploaded_available",media.has_image);
        cJSON_AddStringToObject(value,"source",media.has_image &&
            hisu_settings_get(HISU_SETTING_WALLPAPER_DEFAULT) != 1 ? "uploaded" : "default");
        if (media.has_image) cJSON_AddStringToObject(value,"uploaded_type",media.gif ? "gif" : "image");
    } else if (!strcmp(name->valuestring,"set_wallpaper")) {
        const cJSON *source = cJSON_GetObjectItemCaseSensitive(args,"source");
        valid = cJSON_IsObject(args) && cJSON_GetArraySize(args) == 1 && cJSON_IsString(source) &&
            (!strcmp(source->valuestring,"default") || !strcmp(source->valuestring,"uploaded"));
        if (valid) {
            hisu_media_status_t media;
            hisu_media_get(&media);
            setting = HISU_SETTING_WALLPAPER_DEFAULT;
            setting_value = !strcmp(source->valuestring,"default");
            if (!setting_value && !media.has_image) {
                err = ESP_ERR_NOT_FOUND;
                cJSON_AddStringToObject(value,"detail","No uploaded wallpaper is stored");
            }
        }
    } else if (!strcmp(name->valuestring,"request_reboot") && no_args) {
        portENTER_CRITICAL(&s_mux);
        valid = s_active && frame->epoch == s_epoch && frame->generation == s_generation;
        if (valid) {
            s_reboot_pending = true; s_reboot_generation = frame->generation;
            s_reboot_deadline = esp_timer_get_time() + 60000000;
        }
        portEXIT_CRITICAL(&s_mux);
        cJSON_AddBoolToObject(value,"confirmation_required",true);
        cJSON_AddStringToObject(value,"confirmation_phrase","xác nhận khởi động lại");
    } else if (!strcmp(name->valuestring,"confirm_reboot") && no_args) {
        portENTER_CRITICAL(&s_mux);
        valid = s_reboot_pending && s_active && frame->epoch == s_epoch &&
            frame->generation == s_generation && frame->generation != s_reboot_generation &&
            esp_timer_get_time() <= s_reboot_deadline && reboot_confirmed();
        portEXIT_CRITICAL(&s_mux);
        if (valid) *end_session = true;
        else cJSON_AddStringToObject(value,"detail","A pending request and a new explicit user confirmation are required");
    } else if (!strcmp(name->valuestring,"set_expression")) {
        const cJSON *expression = cJSON_GetObjectItemCaseSensitive(args,"expression");
        hisu_face_t face;
        valid = cJSON_IsObject(args) && cJSON_GetArraySize(args) == 1 &&
            cJSON_IsString(expression) && hisu_face_parse(expression->valuestring, &face);
        if (valid && !queue_cue(frame->epoch, frame->generation, 2, expression->valuestring))
            err = ESP_ERR_NO_MEM;
    } else if (!strcmp(name->valuestring,"get_device_settings") && no_args) {
        valid = true;
        cJSON_AddNumberToObject(value,"volume",hisu_settings_get(HISU_SETTING_VOLUME));
        cJSON_AddNumberToObject(value,"brightness",hisu_settings_get(HISU_SETTING_BRIGHTNESS));
        cJSON_AddBoolToObject(value,"speaker",hisu_settings_get(HISU_SETTING_SPEAKER) == 1);
        cJSON_AddStringToObject(value,"theme",hisu_settings_get(HISU_SETTING_THEME) == 1 ? "light" : "dark");
        cJSON_AddNumberToObject(value,"screen_timeout",hisu_settings_get(HISU_SETTING_TIMEOUT));
    } else if (!strcmp(name->valuestring,"end_conversation") && no_args) {
        valid = true;
        *end_session = true;
    } else if (!strcmp(name->valuestring,"set_volume")) {
        setting = HISU_SETTING_VOLUME;
        valid = tool_integer(args,"percent",0,100,&setting_value);
    } else if (!strcmp(name->valuestring,"set_brightness")) {
        setting = HISU_SETTING_BRIGHTNESS;
        valid = tool_integer(args,"percent",10,100,&setting_value);
    } else if (!strcmp(name->valuestring,"set_screen_timeout")) {
        setting = HISU_SETTING_TIMEOUT;
        valid = tool_integer(args,"seconds",0,600,&setting_value) &&
            (setting_value == 0 || setting_value == 30 || setting_value == 60 ||
             setting_value == 120 || setting_value == 300 || setting_value == 600);
    } else if (!strcmp(name->valuestring,"set_theme")) {
        const cJSON *theme = cJSON_GetObjectItemCaseSensitive(args,"theme");
        setting = HISU_SETTING_THEME;
        valid = cJSON_IsObject(args) && cJSON_GetArraySize(args) == 1 && cJSON_IsString(theme) &&
            (!strcmp(theme->valuestring,"light") || !strcmp(theme->valuestring,"dark"));
        if (valid) setting_value = !strcmp(theme->valuestring,"light");
    } else if (!strcmp(name->valuestring,"set_speaker") || !strcmp(name->valuestring,"set_flashlight")) {
        const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(args,"enabled");
        valid = cJSON_IsObject(args) && cJSON_GetArraySize(args) == 1 && cJSON_IsBool(enabled);
        if (valid && !strcmp(name->valuestring,"set_flashlight")) {
            s_callback(LIVE_FLASHLIGHT,frame->generation,cJSON_IsTrue(enabled) ? "on" : "off");
            cJSON_AddBoolToObject(value,"enabled",cJSON_IsTrue(enabled));
        } else if (valid) {
            setting = HISU_SETTING_SPEAKER;
            setting_value = cJSON_IsTrue(enabled);
        }
    }
    if (valid && err == ESP_OK && setting != HISU_SETTING_COUNT) {
        err = hisu_settings_set(setting,setting_value);
        changed = err == ESP_OK;
        if (changed) cJSON_AddNumberToObject(value,"value",hisu_settings_get(setting));
    }
    if (!current(frame->epoch,frame->generation)) {
        if (s_pending_music) { hisu_music_finish(s_pending_music->ticket); free(s_pending_music); s_pending_music=NULL; }
        *end_session=false; cJSON_Delete(response); cJSON_Delete(call); return NULL;
    }
    cJSON_AddBoolToObject(value,"ok",valid && err == ESP_OK);
    ESP_LOGI(TAG,"Device tool %s: %s",name->valuestring, valid && err == ESP_OK ? "ok" : "rejected");
    if (!valid) cJSON_AddStringToObject(value,"error","Unsupported function or invalid arguments");
    else if (err != ESP_OK) cJSON_AddStringToObject(value,"error",
        !strcmp(name->valuestring,"search_music") || !strcmp(name->valuestring,"play_music") ?
        "Zing MP3 unavailable, track restricted, or request cancelled. Do not substitute another song." :
        "Device could not apply the request");
    char *body = cJSON_PrintUnformatted(response);
    if (changed) s_callback(LIVE_DEVICE,frame->generation,"");
    cJSON_Delete(response); cJSON_Delete(call);
    return body;
}

static bool run_tool(esp_websocket_client_handle_t client, const input_t *frame)
{
    ESP_LOGI(TAG,"Device tool start: live_send minimum stack free=%u bytes",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    bool end_session;
    char *response = execute_tool(frame,&end_session);
    ESP_LOGI(TAG,"Device tool finished: live_send minimum stack free=%u bytes",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    if (!response && !current(frame->epoch,frame->generation)) return true;
    if (response) {
        portENTER_CRITICAL(&s_mux);
        if (frame->epoch == s_epoch && frame->generation == s_generation) {
            if (s_reply_state.pending_tools) --s_reply_state.pending_tools;
            /* A boundary received before this result cannot finish its continuation.
             * Retire before sending: follow-up can arrive while send_text returns. */
            s_reply_state.deferred_complete = false;
        }
        portEXIT_CRITICAL(&s_mux);
    }
    bool ok = response && send_text(client,response);
    cJSON_free(response);
    /* Reboot only after acknowledging a confirmed request on this still-current turn. */
    cJSON *call = end_session ? cJSON_Parse((const char *)frame->pcm) : NULL;
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(call,"name");
    bool reboot = ok && current(frame->epoch,frame->generation) && cJSON_IsString(name) &&
        !strcmp(name->valuestring,"confirm_reboot");
    if (s_pending_music && (!ok || !current(frame->epoch,frame->generation))) {
        hisu_music_finish(s_pending_music->ticket); free(s_pending_music); s_pending_music=NULL;
        end_session=false;
    }
    cJSON_Delete(call);
    if (end_session) s_callback(LIVE_END,frame->generation,"");
    if (ok && !end_session)
        ESP_LOGI(TAG,"Tool result sent; awaiting Gemini continuation");
    if (reboot) { vTaskDelay(pdMS_TO_TICKS(500)); esp_restart(); }
    return ok;
}

static void transport_task(void *arg)
{
    (void)arg;
    input_t *frame = &s_send_workspace->frame;
    char *key = s_send_workspace->key, *headers = s_send_workspace->headers;
    int16_t *pcm = (int16_t *)s_send_storage;
    char *encoded = s_send_storage + SEND_SAMPLES * sizeof(*pcm);
    char *body = encoded + ENCODED_CAPACITY;
    const char *setup = voice_live_setup;
    for (;;) {
        portENTER_CRITICAL(&s_mux);
        bool active = s_active && !s_failed; uint32_t epoch = s_epoch, generation = s_generation;
        bool music_playing = s_music_playing;
        portEXIT_CRITICAL(&s_mux);
        if (!active || music_playing) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (hisu_settings_get_gemini_key(key) != ESP_OK) { fail(epoch, generation); continue; }
        snprintf(headers, sizeof(s_send_workspace->headers), "x-goog-api-key: %s\r\n", key);
        memset(key, 0, sizeof(s_send_workspace->key));
        connection_t connection = {.epoch = epoch};
        connection.message = heap_caps_malloc(MESSAGE_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        bool have_transport = connection.message && create_connection_transport(&connection, headers);
        esp_websocket_client_config_t config = {
            .uri = "wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent",
            .headers = headers, .crt_bundle_attach = esp_crt_bundle_attach,
            .ext_transport = connection.ws,
            .disable_auto_reconnect = true, .buffer_size = WSS_BUFFER_BYTES, .task_stack = 6144,
            .task_prio = 4, .network_timeout_ms = 10000,
        };
        esp_websocket_client_handle_t client = have_transport ? esp_websocket_client_init(&config) : NULL;
        memset(headers, 0, sizeof(s_send_workspace->headers));
        if (!client) {
            destroy_connection_transport(&connection);
            free(connection.message); fail(epoch, generation); continue;
        }
        if (hisu_network_set_performance(true) != ESP_OK) ESP_LOGW(TAG, "Cannot request Wi-Fi performance mode");
        esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event, &connection);
        if (esp_websocket_client_start(client) != ESP_OK) fail(epoch, generation);
        bool sent_setup = false;
        int64_t response_deadline = 0;
        int64_t started = esp_timer_get_time();
        while (true) {
            portENTER_CRITICAL(&s_mux);
            active = s_active && !s_failed && epoch == s_epoch;
            generation = s_generation;
            bool response_pending = s_turn.response_pending;
            int64_t interrupt_deadline = s_interrupt_deadline;
            portEXIT_CRITICAL(&s_mux);
            if (!active) break;
            if ((response_pending && response_deadline && esp_timer_get_time() >= response_deadline) ||
                (interrupt_deadline && esp_timer_get_time() >= interrupt_deadline)) {
                fail(epoch, generation); break;
            }
            if (!voice_live_ready() && esp_timer_get_time() - started > 15000000) { fail(epoch, generation); break; }
            portENTER_CRITICAL(&s_mux); bool connected = connection.connected; portEXIT_CRITICAL(&s_mux);
            if (connected && !sent_setup) {
                sent_setup = send_text(client, setup);
                if (!sent_setup) { fail(epoch, generation); break; }
            }
            if (xQueueReceive(s_input, frame, pdMS_TO_TICKS(10)) != pdTRUE) continue;
            if (!current(frame->epoch, frame->generation)) continue;
            bool ok;
            if (frame->kind == 1 || frame->kind == 4) {
                response_deadline = 0;
                portENTER_CRITICAL(&s_mux);
                if (frame->epoch != s_epoch || frame->generation != s_generation) {
                    portEXIT_CRITICAL(&s_mux); continue;
                }
                unsigned action = live_turn_start(&s_turn);
                s_interrupt_deadline = s_turn.obsolete_turns ? esp_timer_get_time() + 10000000 : 0;
                portEXIT_CRITICAL(&s_mux);
                ok = !(action & LIVE_TURN_END_OLD) || send_text(client, "{\"realtimeInput\":{\"activityEnd\":{}}}");
                if (ok && (action & LIVE_TURN_START)) ok = send_text(client, "{\"realtimeInput\":{\"activityStart\":{}}}");
            }
            else if (frame->kind == 2) {
                portENTER_CRITICAL(&s_mux);
                if (frame->epoch != s_epoch || frame->generation != s_generation) {
                    portEXIT_CRITICAL(&s_mux); continue;
                }
                s_end_us = esp_timer_get_time(); live_turn_end(&s_turn);
                portEXIT_CRITICAL(&s_mux);
                response_deadline = esp_timer_get_time() + 60000000;
                ok = send_text(client, "{\"realtimeInput\":{\"activityEnd\":{}}}");
            } else if (frame->kind == 5) {
                ok = run_tool(client,frame);
            } else if (frame->kind == 6) {
                response_deadline = esp_timer_get_time() + 60000000;
                ok = send_wake(client,frame);
            } else if (frame->kind == 7) {
                response_deadline = esp_timer_get_time() + 60000000;
                ok = send_spoken_continuation(client,frame);
            } else if (frame->kind == 3) {
                ok = send_text(client, (char *)frame->pcm);
            } else {
                size_t samples = collect_audio(frame, pcm, &s_send_workspace->peek);
                size_t length;
                int err = mbedtls_base64_encode((unsigned char *)encoded, ENCODED_CAPACITY, &length,
                                               (unsigned char *)pcm, samples * sizeof(*pcm));
                if (err || length >= ENCODED_CAPACITY) { fail(epoch, generation); break; }
                format_audio(body, encoded, length);
                ok = send_text(client, body);
            }
            if (!ok) { fail(epoch, generation); break; }
        }
        esp_websocket_client_stop(client);
        esp_websocket_client_destroy(client);
        destroy_connection_transport(&connection);
        free(connection.message);
        if (s_pending_music) {
            hisu_music_track_t *track=s_pending_music; s_pending_music=NULL;
            produce_music(track);
        } else if (hisu_network_set_performance(false) != ESP_OK)
            ESP_LOGW(TAG, "Cannot restore Wi-Fi power saving");
    }
}
static esp_err_t speaker_reserve(void)
{
    if (s_speaker) return ESP_OK;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    /* PCM is queued in PSRAM; keep the hardware ring small enough for concurrent TLS. */
    channel.dma_desc_num = 4;
    channel.dma_frame_num = 160;
    /* Do not replay old DMA samples when streamed audio arrives late. */
    channel.auto_clear_after_cb = true;
    esp_err_t err = i2s_new_channel(&channel, &s_speaker, NULL);
    if (err != ESP_OK) return err;
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(24000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = SPK_BCLK, .ws = SPK_LRCK, .dout = SPK_DOUT, .din = I2S_GPIO_UNUSED},
    };
    err = i2s_channel_init_std_mode(s_speaker, &config);
    if (err == ESP_OK) { s_speaker_rate=24000; log_audio_heap("Speaker reserved RAM"); }
    if (err != ESP_OK) {
        i2s_del_channel(s_speaker); s_speaker = NULL;
        log_audio_heap("Speaker allocation failure RAM");
    }
    return err;
}
static void speaker_close(void)
{
    if (s_speaker && s_speaker_enabled) {
        i2s_channel_disable(s_speaker); s_speaker_enabled=false;
    }
}
static void speaker_release(void)
{
    speaker_close();
    if (s_speaker) { i2s_del_channel(s_speaker); s_speaker=NULL; }
    s_speaker_rate=0;
}
static esp_err_t speaker_open_rate(int rate)
{
    esp_err_t err = speaker_reserve();
    if (err != ESP_OK) return err;
    if (rate != s_speaker_rate) {
        speaker_close();
        i2s_std_clk_config_t clock = I2S_STD_CLK_DEFAULT_CONFIG(rate);
        err = i2s_channel_reconfig_std_clock(s_speaker,&clock);
        if (err != ESP_OK) return err;
        s_speaker_rate=rate;
    }
    if (!s_speaker_enabled) {
        /* A retained channel must never replay samples left by an interrupted reply. */
        static const uint8_t silence[4*160*4] = {0};
        size_t loaded = 0;
        err = i2s_channel_preload_data(s_speaker,silence,sizeof(silence),&loaded);
        if (err != ESP_OK || loaded != sizeof(silence)) {
            speaker_release(); return err != ESP_OK ? err : ESP_FAIL;
        }
        err = i2s_channel_enable(s_speaker);
        if (err == ESP_OK) { s_speaker_enabled=true; log_audio_heap("Speaker ready RAM"); }
        else speaker_release();
    }
    return err;
}
static esp_err_t speaker_open(void) { return speaker_open_rate(24000); }
static bool speaker_write(const int16_t *pcm, size_t bytes)
{
    size_t written = 0;
    /* I2S takes milliseconds, unlike FreeRTOS queue and WebSocket APIs. */
    esp_err_t err = s_speaker && s_speaker_enabled ? i2s_channel_write(s_speaker, pcm, bytes, &written, 100) : ESP_ERR_INVALID_STATE;
    if (err != ESP_OK || written != bytes) {
        ESP_LOGW(TAG, "Speaker write failed: %s, bytes=%u/%u", esp_err_to_name(err),
                 (unsigned)written, (unsigned)bytes);
        return false;
    }
    return true;
}
static bool music_write(const int16_t *pcm, size_t samples, int rate, int channels)
{
    if (speaker_open_rate(rate) != ESP_OK) return false;
    if (!s_music_started && s_music_output) {
        char title[256]; snprintf(title,sizeof(title),"%s\n%s",s_music_output->title,s_music_output->artist);
        s_callback(LIVE_MUSIC,s_music_output->ticket,title); s_music_started=true;
    }
    int16_t *stereo=s_speaker_mix;
    size_t frames=samples/(size_t)channels;
    for (size_t offset=0;offset<frames;) {
        size_t n=frames-offset; if (n>256) n=256;
        int volume=hisu_settings_get(HISU_SETTING_SPEAKER) == 1 ? hisu_settings_get(HISU_SETTING_VOLUME) : 0;
        for (size_t i=0;i<n;++i) {
            size_t p=(offset+i)*(size_t)channels;
            int32_t mono=channels==2 ? ((int32_t)pcm[p]+pcm[p+1])/2 : pcm[p];
            stereo[i*2]=stereo[i*2+1]=(int16_t)(mono*volume/100);
        }
        if (!speaker_write(stereo,n*4)) return false;
        offset+=n;
    }
    return true;
}

static uint64_t music_duration(size_t bytes, int rate, int channels)
{
    return (uint64_t)bytes*1000000/(2*(unsigned)channels*(unsigned)rate);
}

static bool music_enqueue(const int16_t *pcm, size_t samples, int rate, int channels)
{
    uint32_t ticket=s_music_producer_ticket;
    if (!hisu_music_current(ticket) || !samples || samples>16384 ||
        (channels!=1 && channels!=2) || rate<8000 || rate>48000 || samples%(unsigned)channels) return false;
    size_t bytes=samples*sizeof(*pcm);
    int16_t *copy=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!copy) return false;
    memcpy(copy,pcm,bytes);
    uint64_t duration=music_duration(bytes,rate,channels);
    output_t item={.music=true,.generation=ticket,.pcm=copy,.bytes=bytes,.rate=rate,.channels=channels};
    while (hisu_music_current(ticket)) {
        portENTER_CRITICAL(&s_mux);
        bool room=s_music_queued_bytes+bytes<=128*1024;
        if (room) { s_music_queued_bytes+=bytes; s_music_queued_us+=duration; }
        portEXIT_CRITICAL(&s_mux);
        if (room) {
            if (xQueueSend(s_output,&item,pdMS_TO_TICKS(20))==pdTRUE) return true;
            portENTER_CRITICAL(&s_mux);
            s_music_queued_bytes-=bytes; s_music_queued_us-=duration;
            portEXIT_CRITICAL(&s_mux);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    free(copy); return false;
}

static void produce_music(hisu_music_track_t *track)
{
    portENTER_CRITICAL(&s_mux);
    s_music_playing=true; s_music_producer_done=false;
    s_music_queued_bytes=0; s_music_queued_us=0;
    s_music_producer_ticket=track->ticket;
    portEXIT_CRITICAL(&s_mux);
    output_t start={.music=true,.cue=3,.generation=track->ticket,.pcm=(int16_t *)track};
    if (!hisu_music_current(track->ticket) || xQueueSend(s_output,&start,pdMS_TO_TICKS(100))!=pdTRUE) {
        if (hisu_music_current(track->ticket)) s_callback(LIVE_MUSIC_ERROR,track->ticket,"Không bắt đầu được phát nhạc");
        hisu_music_finish(track->ticket); free(track);
        (void)hisu_network_set_performance(false);
        portENTER_CRITICAL(&s_mux); s_music_playing=false; portEXIT_CRITICAL(&s_mux);
        return;
    }
    (void)hisu_network_set_performance(true);
    s_callback(LIVE_MUSIC_LOADING,track->ticket,"");
    log_audio_heap("Music resolve after WSS teardown RAM");
    esp_err_t result=hisu_music_resolve(track);
    if (result==ESP_OK && hisu_music_current(track->ticket)) result=hisu_music_play(track,music_enqueue);
    ESP_LOGI(TAG,"Music producer finished: result=%d, minimum stack free=%u bytes",
        (int)result,(unsigned)uxTaskGetStackHighWaterMark(NULL));
    /* The playback worker owns track after the start marker and frees it after draining. */
    portENTER_CRITICAL(&s_mux);
    s_music_result=result; s_music_producer_done=true;
    portEXIT_CRITICAL(&s_mux);
}

static bool wait_audio_buffer(bool music, bool startup, uint32_t epoch, uint32_t generation,
    size_t current_bytes, uint64_t current_us)
{
    int64_t started=esp_timer_get_time();
    uint64_t target_us=startup ? 500000 : 60000;
    int64_t wait_us=startup ? 750000 : 80000;
    while (music ? hisu_music_current(generation) : current(epoch,generation)) {
        portENTER_CRITICAL(&s_mux);
        bool ready=music ? s_music_queued_us+current_us>=target_us :
            s_queued_audio_bytes+current_bytes>=target_us*48/1000; /* 24 kHz mono, 16-bit. */
        bool done=music ? s_music_producer_done : !s_turn.response_pending;
        portEXIT_CRITICAL(&s_mux);
        if (ready || done || esp_timer_get_time()-started>=wait_us) return true;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

static void consume_music(const output_t *item)
{
    if (item->cue==3) {
        speaker_close();
        s_music_output=(hisu_music_track_t *)item->pcm;
        s_music_started=false; s_music_needs_buffer=true;
        return;
    }
    uint64_t duration=music_duration(item->bytes,item->rate,item->channels);
    portENTER_CRITICAL(&s_mux);
    s_music_queued_bytes-=item->bytes; s_music_queued_us-=duration;
    portEXIT_CRITICAL(&s_mux);
    if (s_music_output && item->generation==s_music_output->ticket && hisu_music_current(item->generation)) {
        bool ready=!s_music_needs_buffer || wait_audio_buffer(true,!s_music_started,0,item->generation,item->bytes,duration);
        if (ready && hisu_music_current(item->generation)) {
            if (!music_write(item->pcm,item->bytes/2,item->rate,item->channels)) {
                if (hisu_music_current(item->generation))
                    s_callback(LIVE_MUSIC_ERROR,item->generation,"Không phát được âm thanh");
                hisu_music_finish(item->generation);
            }
            s_music_needs_buffer=false;
        }
    }
    free(item->pcm);
}

static bool finish_music_output(void)
{
    if (!s_music_output) return false;
    portENTER_CRITICAL(&s_mux);
    bool done=s_music_producer_done && !s_music_queued_bytes;
    esp_err_t result=s_music_result;
    portEXIT_CRITICAL(&s_mux);
    if (!done) return false;
    uint32_t ticket=s_music_output->ticket;
    if (s_music_started && hisu_music_current(ticket)) vTaskDelay(pdMS_TO_TICKS(100));
    speaker_close();
    if (hisu_music_current(ticket)) s_callback(result==ESP_OK ? LIVE_MUSIC : LIVE_MUSIC_ERROR,ticket,
        result==ESP_OK ? "" : "Không phát được nhạc, gọi Hi Su để thử lại");
    hisu_music_finish(ticket); free((void *)s_music_output); s_music_output=NULL;
    (void)hisu_network_set_performance(false);
    portENTER_CRITICAL(&s_mux); s_music_playing=false; portEXIT_CRITICAL(&s_mux);
    return true;
}

static void playback_task(void *arg)
{
    (void)arg;
    output_t audio;
    bool first = true;
    bool needs_buffer=true;
    playback_caption_t caption = {0};
    uint32_t playback_epoch = 0, playback_generation = 0;
    int16_t *stereo=s_speaker_mix;
    for (;;) {
        portENTER_CRITICAL(&s_mux);
        uint32_t epoch = s_epoch, generation = s_generation;
        bool active = s_active && !s_failed;
        portEXIT_CRITICAL(&s_mux);
        if (!s_music_output && (!active || epoch != playback_epoch || generation != playback_generation)) {
            speaker_close(); first = needs_buffer = true;
            playback_epoch = epoch; playback_generation = generation;
        }
        if (xQueueReceive(s_output, &audio, pdMS_TO_TICKS(20)) != pdTRUE) {
            if (s_music_output) {
                if (!finish_music_output() && s_music_started && !s_music_needs_buffer) {
                    s_music_needs_buffer=true;
                    ESP_LOGW(TAG,"Music queue starved; buffering before resume");
                }
                if (s_music_output && !hisu_music_current(s_music_output->ticket)) speaker_close();
                continue;
            }
            portENTER_CRITICAL(&s_mux); bool active = s_active && !s_failed; portEXIT_CRITICAL(&s_mux);
            if (!active) { speaker_close(); first = needs_buffer = true; }
            else if (!first && !needs_buffer) {
                needs_buffer=true;
                int64_t now=esp_timer_get_time();
                portENTER_CRITICAL(&s_mux);
                size_t received=s_reply_state.rx_bytes;
                int64_t span=now-s_reply_state.rx_first_us, gap=now-s_reply_state.rx_last_us;
                int64_t max_gap=s_reply_state.max_rx_gap_us;
                portEXIT_CRITICAL(&s_mux);
                ESP_LOGW(TAG,"Voice queue starved: received=%u ms over %lld ms, since_rx=%lld ms, max_rx_gap=%lld ms; resume wait<=80 ms",
                    (unsigned)(received/48),(long long)(span/1000),(long long)(gap/1000),(long long)(max_gap/1000));
            }
            continue;
        }
        if (audio.music) {
            consume_music(&audio); first=needs_buffer=true; continue;
        }
        if (audio.bytes) { portENTER_CRITICAL(&s_mux); s_queued_audio_bytes -= audio.bytes; portEXIT_CRITICAL(&s_mux); }
        if (!current(audio.epoch, audio.generation)) { free(audio.pcm); speaker_close(); first = true; continue; }
        if (audio.cue) {
            if (audio.cue == 1) hold_caption(&caption, &audio);
            else play_cue(&audio);
            free(audio.pcm); continue;
        }
        if (audio.done) {
            if (!first) flush_caption(&caption, audio.epoch, audio.generation);
            else caption.text[0] = 0;
            if (s_speaker_enabled) vTaskDelay(pdMS_TO_TICKS(100));
            speaker_close(); first = needs_buffer = true;
            if (current(audio.epoch, audio.generation)) s_callback(LIVE_DONE, audio.generation, "");
            continue;
        }
        if (needs_buffer && !wait_audio_buffer(false,first,audio.epoch,audio.generation,audio.bytes,0)) {
            free(audio.pcm); continue;
        }
        needs_buffer=false;
        if (first) {
            portENTER_CRITICAL(&s_mux); int64_t ended_us = s_end_us; portEXIT_CRITICAL(&s_mux);
            ESP_LOGI(TAG, "First audio: %lld ms after request", (long long)((esp_timer_get_time() - ended_us) / 1000));
            s_callback(LIVE_AUDIO, audio.generation, ""); first = false;
        }
        flush_caption(&caption, audio.epoch, audio.generation);
        if (hisu_settings_get(HISU_SETTING_SPEAKER) == 1) {
            esp_err_t speaker_err = speaker_open();
            if (speaker_err != ESP_OK) {
                ESP_LOGW(TAG, "Speaker open failed: %s", esp_err_to_name(speaker_err));
                fail(audio.epoch, audio.generation);
            }
            size_t offset = 0;
            while (offset < audio.bytes / 2 && current(audio.epoch, audio.generation)) {
                size_t count = audio.bytes / 2 - offset; if (count > 256) count = 256;
                int volume = hisu_settings_get(HISU_SETTING_VOLUME);
                for (size_t i = 0; i < count; ++i) stereo[2*i] = stereo[2*i+1] = (int32_t)audio.pcm[offset+i] * volume / 100;
                if (!speaker_write(stereo, count * 4)) {
                    fail(audio.epoch, audio.generation); break;
                }
                offset += count;
            }
        }
        free(audio.pcm);
    }
}
esp_err_t voice_live_init(live_event_fn callback)
{
    s_callback = callback;
    s_send_storage = heap_caps_malloc(SEND_SAMPLES * sizeof(int16_t) + ENCODED_CAPACITY + SEND_BODY_CAPACITY,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_send_workspace = heap_caps_calloc(1, sizeof(*s_send_workspace), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_input_storage = heap_caps_malloc(INPUT_QUEUE_FRAMES * sizeof(input_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    /* Text cues must not halve the audio buffering capacity or consume scarce internal RAM. */
    s_output_storage = heap_caps_malloc(96 * sizeof(output_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_speaker_mix = heap_caps_malloc(512*sizeof(*s_speaker_mix),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!s_input_storage || !s_send_storage || !s_send_workspace || !s_output_storage || !s_speaker_mix) {
        voice_live_deinit(); return ESP_ERR_NO_MEM;
    }
    s_input = xQueueCreateStatic(INPUT_QUEUE_FRAMES, sizeof(input_t), s_input_storage, &s_input_control);
    s_output = xQueueCreateStatic(96, sizeof(output_t), s_output_storage, &s_output_control);
    if (!s_input || !s_output) { voice_live_deinit(); return ESP_ERR_NO_MEM; }
    /* Reserve scarce DMA before WSS and music HTTPS can compete for internal RAM. */
    esp_err_t speaker_err = speaker_reserve();
    if (speaker_err != ESP_OK) { voice_live_deinit(); return speaker_err; }
    if (xTaskCreate(transport_task, "live_send", 8192, NULL, 4, &s_transport_task) != pdPASS) {
        voice_live_deinit(); return ESP_ERR_NO_MEM;
    }
    /* I2S deadlines take priority over wake recognition; network/decode stay on live_send. */
    if (xTaskCreate(playback_task, "live_play", 6144, NULL, 6, &s_playback_task) != pdPASS) {
        voice_live_deinit(); return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
/* Startup rollback only, before microphone/detection tasks can open a session. */
void voice_live_deinit(void)
{
    if (s_transport_task) { vTaskDelete(s_transport_task); s_transport_task = NULL; }
    if (s_playback_task) { vTaskDelete(s_playback_task); s_playback_task = NULL; }
    speaker_release();
    if (s_input) { vQueueDelete(s_input); s_input = NULL; }
    if (s_output) { vQueueDelete(s_output); s_output = NULL; }
    free(s_input_storage); s_input_storage = NULL;
    free(s_output_storage); s_output_storage = NULL;
    free(s_send_storage); s_send_storage = NULL;
    free(s_send_workspace); s_send_workspace = NULL;
    free(s_speaker_mix); s_speaker_mix=NULL;
}
