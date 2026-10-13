#include "hisu_voice.h"
#include "hisu_voice_session.h"
#include "hisu_voice_live.h"
#include "hisu_ui.h"
#include "hisu_network.h"
#include "hisu_settings.h"
#include "board_config.h"
#include "driver/i2s_std.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "model_path.h"
#include <stdlib.h>
#include <string.h>
static const char *TAG = "VOICE";
static i2s_chan_handle_t s_rx;
static const esp_afe_sr_iface_t *s_afe;
static esp_afe_sr_data_t *s_afe_data;
static esp_mn_iface_t *s_multinet;
static model_iface_data_t *s_multinet_data;
static SemaphoreHandle_t s_lock;
static voice_session_t s_session;
static char s_reply[256];
static char s_utterance[256];
#define ASR_SAMPLE_RATE 16000
#define ASR_MAX_SAMPLES (ASR_SAMPLE_RATE * 12)
#define ASR_MIN_SAMPLES (ASR_SAMPLE_RATE / 2)
#define ASR_SILENCE_SAMPLES (ASR_SAMPLE_RATE * 7 / 10)
#define ASR_START_VOLUME_DBFS (-35.0f)
#define ASR_START_SAMPLES (ASR_SAMPLE_RATE / 5)
static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static void live_event(live_event_t event, uint32_t generation, const char *text)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!voice_session_current(&s_session, generation)) { xSemaphoreGive(s_lock); return; }
    if (event == LIVE_READY) {
        s_session.ready_ms = now_ms() + 300;
        s_session.deadline_ms = s_session.ready_ms + 15000;
        (void)hisu_ui_post_status(HISU_UI_LISTENING, text);
    } else if (event == LIVE_TEXT) {
        size_t used = strlen(s_reply), extra = strlen(text);
        if (extra > sizeof(s_reply) - used - 1) extra = sizeof(s_reply) - used - 1;
        memcpy(s_reply + used, text, extra); s_reply[used + extra] = 0;
        (void)hisu_ui_post_status(HISU_UI_SPEAKING, s_reply);
    } else if (event == LIVE_AUDIO) {
        s_session.state = VOICE_SPEAKING;
        (void)hisu_ui_post_status(HISU_UI_SPEAKING, s_reply);
    } else if (event == LIVE_DONE) {
        voice_session_finish(&s_session, generation, now_ms());
        (void)hisu_ui_post_status(HISU_UI_LISTENING, s_reply);
    } else if (event == LIVE_INPUT) {
        size_t used = strlen(s_utterance), extra = strlen(text);
        if (extra > sizeof(s_utterance) - used - 1) extra = sizeof(s_utterance) - used - 1;
        memcpy(s_utterance + used, text, extra); s_utterance[used + extra] = 0;
        if (voice_session_end_phrase(s_utterance)) {
            voice_session_close(&s_session); voice_live_close();
            (void)hisu_ui_post_status(HISU_UI_IDLE, "");
        }
    } else if (event == LIVE_FLASHLIGHT) {
        (void)hisu_ui_post_flashlight(strcmp(text, "on") == 0);
    } else if (event == LIVE_ERROR) {
        voice_session_close(&s_session); voice_live_close();
        (void)hisu_ui_post_status(HISU_UI_ERROR, text);
    }
    xSemaphoreGive(s_lock);
}
bool hisu_voice_control(bool end_session)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool active = s_session.state != VOICE_IDLE;
    if (active) {
        if (end_session) { voice_session_close(&s_session); voice_live_close(); }
        else { voice_session_open(&s_session, now_ms()); voice_live_open(s_session.generation); }
        s_reply[0] = 0;
        (void)hisu_ui_post_status(end_session ? HISU_UI_IDLE : HISU_UI_THINKING,
                                  end_session ? "" : "Đang kết nối Gemini Live...");
    }
    xSemaphoreGive(s_lock);
    return active;
}
static void microphone_task(void *arg)
{
    (void)arg;
    const int frames = s_afe->get_feed_chunksize(s_afe_data);
    int16_t *pcm = malloc(frames * sizeof(*pcm));
    int32_t *raw = malloc(frames * 2 * sizeof(*raw));
    if (!pcm || !raw) {
        ESP_LOGE(TAG, "No memory for wake-word audio buffers");
        (void)hisu_ui_post_status(HISU_UI_ERROR, "Không đủ RAM cho mic");
        free(pcm); free(raw);
        vTaskDelete(NULL);
    }
    uint64_t recent_energy[2] = {0};
    unsigned recent_blocks = 0;
    for (;;) {
        size_t filled = 0;
        const size_t needed = frames * 2 * sizeof(*raw);
        while (filled < needed) {
            size_t bytes = 0;
            esp_err_t err = i2s_channel_read(s_rx, (uint8_t *)raw + filled,
                                             needed - filled, &bytes, pdMS_TO_TICKS(1000));
            if (err != ESP_OK || !bytes) {
                ESP_LOGW(TAG, "Microphone read: %s", esp_err_to_name(err));
                filled = 0;
                break;
            }
            filled += bytes;
        }
        if (filled != needed) continue;
        /* I2S MEMS mic occupies one of the two 32-bit slots. Select the
         * active slot by signal energy; preserve 16 kHz mono for the AFE. */
        uint64_t energy[2] = {0};
        for (int i = 0; i < frames; ++i) {
            for (int ch = 0; ch < 2; ++ch) {
                int64_t sample = (int64_t)raw[i * 2 + ch] >> 16;
                energy[ch] += sample < 0 ? -sample : sample;
            }
        }
        int active = energy[1] > energy[0] ? 1 : 0;
        recent_energy[0] += energy[0];
        recent_energy[1] += energy[1];
        if (++recent_blocks >= 160) {
            ESP_LOGI(TAG, "Mic L=%lu R=%lu active=%c (mean abs PCM)",
                     (unsigned long)(recent_energy[0] / ((uint64_t)frames * recent_blocks)),
                     (unsigned long)(recent_energy[1] / ((uint64_t)frames * recent_blocks)),
                     active ? 'R' : 'L');
            recent_energy[0] = recent_energy[1] = 0;
            recent_blocks = 0;
        }
        /* Preserve headroom for close speech; AFE AGC raises quiet speech
         * while compressing louder input. */
        for (int i = 0; i < frames; ++i) {
            int64_t amplified = ((int64_t)raw[i * 2 + active] >> 16) * 12;
            if (amplified > INT16_MAX) amplified = INT16_MAX;
            if (amplified < INT16_MIN) amplified = INT16_MIN;
            pcm[i] = (int16_t)amplified;
        }
        s_afe->feed(s_afe_data, pcm);
    }
}

static void detection_task(void *arg)
{
    (void)arg;
    unsigned fetch_count = 0;
    int64_t last_fetch_us = 0;
    int64_t next_timing_log_us = 0;
    int64_t max_fetch_gap_us = 0;
    int64_t max_detect_us = 0;
    int16_t *capture = NULL;
    size_t captured = 0;
    size_t silence_samples = 0;
    unsigned speech_frames = 0;
    uint32_t capture_generation = 0;
    uint32_t seen_generation = 0;
    const size_t preroll_capacity = ASR_SAMPLE_RATE / 5;
    int16_t *preroll = heap_caps_malloc(preroll_capacity * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t preroll_count = 0;
    if (!preroll) {
        (void)hisu_ui_post_status(HISU_UI_ERROR, "Không đủ RAM nhận giọng nói");
        vTaskDelete(NULL); return;
    }
    int previous_vad = -1;
    for (;;) {
        afe_fetch_result_t *result = s_afe->fetch(s_afe_data);
        int64_t fetched_us = esp_timer_get_time();
        if (last_fetch_us && fetched_us - last_fetch_us > max_fetch_gap_us)
            max_fetch_gap_us = fetched_us - last_fetch_us;
        last_fetch_us = fetched_us;
        if (fetched_us >= next_timing_log_us) {
            ESP_LOGI(TAG, "Audio timing: max fetch gap=%lu ms, MultiNet=%lu ms",
                     (unsigned long)(max_fetch_gap_us / 1000),
                     (unsigned long)(max_detect_us / 1000));
            max_fetch_gap_us = max_detect_us = 0;
            next_timing_log_us = fetched_us + 5000000;
        }
        if (!result || result->ret_value == ESP_FAIL) {
            ESP_LOGE(TAG, "Audio front-end fetch failed");
            (void)hisu_voice_control(true);
            (void)hisu_ui_post_status(HISU_UI_ERROR, "Bộ nhận giọng nói lỗi");
            free(capture); free(preroll);
            vTaskDelete(NULL);
            return;
        }
        /* MultiNet may keep CPU1 busy while Wi-Fi is connecting. Give the
         * idle task a chance to run without delaying every audio frame. */
        if (++fetch_count == 8) {
            fetch_count = 0;
            vTaskDelay(1);
        }
        if ((int)result->vad_state != previous_vad) {
            previous_vad = result->vad_state;
            ESP_LOGI(TAG, "VAD %s, volume %.1f dBFS",
                     result->vad_state == VAD_SPEECH ? "speech" : "silence",
                     result->data_volume);
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (voice_session_expire(&s_session, now_ms())) {
            voice_live_close();
            (void)hisu_ui_post_status(HISU_UI_IDLE, "");
        }
        voice_session_t snapshot = s_session;
        xSemaphoreGive(s_lock);
        if (seen_generation != snapshot.generation) {
            seen_generation = snapshot.generation;
            preroll_count = 0;
            speech_frames = 0;
            s_multinet->clean(s_multinet_data);
        }
        if (capture && !voice_session_current(&snapshot, capture_generation)) {
            free(capture); capture = NULL;
        }
        if (capture) {
            size_t frame = s_multinet->get_samp_chunksize(s_multinet_data);
            if (frame > ASR_MAX_SAMPLES - captured) frame = ASR_MAX_SAMPLES - captured;
            if (!voice_live_feed(capture_generation, result->data, frame)) {
                (void)hisu_voice_control(true);
                free(capture); capture = NULL;
                continue;
            }
            captured += frame;
            /* Keep a generous silence gap for pauses between Vietnamese syllables. */
            if (result->vad_state == VAD_SPEECH) silence_samples = 0;
            else silence_samples += frame;
            if (captured >= ASR_MAX_SAMPLES ||
                (captured >= ASR_MIN_SAMPLES &&
                 silence_samples >= ASR_SILENCE_SAMPLES)) {
                ESP_LOGI(TAG, "Command capture finished after %u ms (%s)",
                         (unsigned)(captured * 1000 / ASR_SAMPLE_RATE),
                         captured >= ASR_MAX_SAMPLES ? "timeout" : "VAD silence");
                free(capture);
                capture = NULL;
                xSemaphoreTake(s_lock, portMAX_DELAY);
                if (voice_session_current(&s_session, capture_generation)) {
                    s_session.state = VOICE_PROCESSING;
                    (void)hisu_ui_post_status(HISU_UI_THINKING, "Đang trả lời...");
                }
                xSemaphoreGive(s_lock);
                (void)voice_live_end(capture_generation);
            }
            continue;
        }
        /* Keep wake detection active during processing and playback for interruption. */
        int64_t detect_started_us = esp_timer_get_time();
        esp_mn_state_t state = s_multinet->detect(s_multinet_data, result->data);
        int64_t detect_us = esp_timer_get_time() - detect_started_us;
        if (detect_us > max_detect_us) max_detect_us = detect_us;
        if (state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *recognized = s_multinet->get_results(s_multinet_data);
            if (recognized && recognized->num > 0)
                ESP_LOGI(TAG, "MultiNet command=%d text=%s confidence=%.2f",
                         recognized->command_id[0], recognized->string, recognized->prob[0]);
            if (recognized && recognized->num > 0 && recognized->command_id[0] == 1) {
                ESP_LOGI(TAG, "Offline Hey Hisu detected (confidence %.2f)", recognized->prob[0]);
                hisu_network_status_t network;
                hisu_network_get(&network);
                if (hisu_voice_control(false)) {
                    /* Interruption works even if connectivity was lost during the turn. */
                } else if (!network.connected) {
                    (void)hisu_ui_post_status(HISU_UI_ERROR, "Cần Wi-Fi để nghe lệnh");
                } else if (hisu_network_has_internet_warning(&network)) {
                    (void)hisu_ui_post_status(HISU_UI_ERROR, network.internet == HISU_INTERNET_CAPTIVE
                        ? "Wi-Fi yêu cầu đăng nhập" : "Wi-Fi chưa có Internet");
                } else if (!hisu_settings_has_gemini_key()) {
                    (void)hisu_ui_post_status(HISU_UI_ERROR, "Cần Gemini API key trong Setting");
                } else {
                    xSemaphoreTake(s_lock, portMAX_DELAY);
                    voice_session_open(&s_session, now_ms());
                    voice_live_open(s_session.generation);
                    (void)hisu_ui_post_status(HISU_UI_THINKING, "Đang kết nối Gemini Live...");
                    xSemaphoreGive(s_lock);
                }
                s_multinet->clean(s_multinet_data);
                continue;
            }
            s_multinet->clean(s_multinet_data);
        } else if (state == ESP_MN_STATE_TIMEOUT) {
            esp_mn_results_t *candidate = s_multinet->get_results(s_multinet_data);
            if (candidate && candidate->num > 0)
                ESP_LOGI(TAG, "MultiNet timeout candidate=%s confidence=%.2f",
                         candidate->raw_string, candidate->prob[0]);
            s_multinet->clean(s_multinet_data);
        }
        if (snapshot.state != VOICE_WAITING || now_ms() < snapshot.ready_ms || !voice_live_ready()) continue;
        size_t frame = s_afe->get_fetch_chunksize(s_afe_data);
        if (frame > preroll_capacity) frame = preroll_capacity;
        if (preroll_count + frame > preroll_capacity) {
            size_t discard = preroll_count + frame - preroll_capacity;
            memmove(preroll, preroll + discard, (preroll_count - discard) * sizeof(int16_t));
            preroll_count -= discard;
        }
        memcpy(preroll + preroll_count, result->data, frame * sizeof(int16_t));
        preroll_count += frame;
        if (result->vad_state == VAD_SPEECH && result->data_volume >= ASR_START_VOLUME_DBFS)
            speech_frames += frame;
        else speech_frames = 0;
        if (speech_frames < ASR_START_SAMPLES) continue;
        ESP_LOGI(TAG, "Capture start: sustained speech, volume %.1f dBFS", result->data_volume);
        capture = heap_caps_malloc(preroll_capacity * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (capture && s_session.generation == snapshot.generation && voice_session_capture(&s_session, now_ms())) {
            capture_generation = s_session.generation;
            seen_generation = capture_generation;
            s_multinet->clean(s_multinet_data);
            captured = preroll_count;
            memcpy(capture, preroll, captured * sizeof(int16_t));
            silence_samples = 0;
            speech_frames = 0;
            s_reply[0] = 0;
            s_utterance[0] = 0;
            (void)hisu_ui_post_status(HISU_UI_LISTENING, "Đang nghe...");
        } else {
            free(capture); capture = NULL;
            if (!capture && s_session.generation == snapshot.generation) {
                voice_session_close(&s_session);
                voice_live_close();
                (void)hisu_ui_post_status(HISU_UI_ERROR, "Không bắt đầu được ghi âm");
            }
        }
        xSemaphoreGive(s_lock);
        if (capture) {
            if (!voice_live_begin(capture_generation) ||
                !voice_live_feed(capture_generation, capture, captured)) {
                (void)hisu_voice_control(true);
                free(capture); capture = NULL;
            }
        }
    }
}

esp_err_t hisu_voice_start(void)
{
    if (s_rx) return ESP_ERR_INVALID_STATE;
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models) return ESP_ERR_NOT_FOUND;
    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    if (!mn_name) {
        ESP_LOGE(TAG, "No English MultiNet model in model partition");
        esp_srmodel_deinit(models);
        return ESP_ERR_NOT_FOUND;
    }
    s_multinet = esp_mn_handle_from_name(mn_name);
    s_multinet_data = s_multinet ? s_multinet->create(mn_name, 6000) : NULL;
    if (!s_multinet_data) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_mn_commands_alloc(s_multinet, s_multinet_data);
    /* The owner says each syllable separately. English G2P merges Hisu and
     * voices its s as /z/, so provide /s/ and syllable boundaries directly. */
    if (err == ESP_OK) err = esp_mn_commands_phoneme_add(1, "hey hi su", "hd hm So");
    if (err == ESP_OK) err = esp_mn_commands_phoneme_add(1, "eh hi su", "f hm So");
    if (err == ESP_OK) err = esp_mn_commands_phoneme_add(1, "ay hi su", "d hm So");
    if (err == ESP_OK) err = esp_mn_commands_phoneme_add(1, "he hi su", "hf hm So");
    if (err == ESP_OK) err = esp_mn_commands_phoneme_add(1, "hello hi su", "hf Lb hm So");
    if (err == ESP_OK) err = esp_mn_commands_phoneme_add(1, "oi hi su", "u hm So");
    if (err == ESP_OK) err = esp_mn_commands_phoneme_add(1, "uh ai hi su", "ci hm So");
    if (err == ESP_OK) err = esp_mn_commands_phoneme_add(1, "hi su", "hm So");
    if (err == ESP_OK) err = esp_mn_commands_phoneme_add(1, "hi su hi su", "hm So hm So");
    if (err == ESP_OK) {
        esp_mn_error_t *command_error = esp_mn_commands_update();
        if (command_error && command_error->num) err = ESP_ERR_INVALID_ARG;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MultiNet cannot recognize the custom phrase 'hey hisu'");
        return err;
    }
    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (!afe_config) return ESP_ERR_NO_MEM;
    afe_config->wakenet_init = false;
    afe_config->agc_init = true;
    afe_config->agc_mode = AFE_AGC_MODE_WEBRTC;
    afe_config->agc_compression_gain_db = 9;
    afe_config->agc_target_level_dbfs = 6;
    afe_config->ns_init = true;
    afe_config->afe_ns_mode = AFE_NS_MODE_WEBRTC;
    afe_config->afe_linear_gain = 1.0f;
    /* Let the capture loop own the 700 ms end-of-speech hangover. */
    afe_config->vad_init = true;
    afe_config->vad_mode = VAD_MODE_3;
    afe_config->vad_min_noise_ms = 128;
    /* Keep the AFE processor on the feed core, above the microphone task.
     * MultiNet must not occupy the core that drains the input ringbuffer. */
    afe_config->afe_perferred_core = 0;
    afe_config->afe_perferred_priority = 6;
    s_afe = esp_afe_handle_from_config(afe_config);
    s_afe_data = s_afe ? s_afe->create_from_config(afe_config) : NULL;
    afe_config_free(afe_config);
    if (!s_afe_data) return ESP_ERR_NO_MEM;
    if (s_afe->get_fetch_chunksize(s_afe_data) != s_multinet->get_samp_chunksize(s_multinet_data)) {
        ESP_LOGE(TAG, "AFE and MultiNet frame sizes differ");
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI(TAG, "Offline phrase: Hey Hisu (%s)", mn_name);
    ESP_LOGI(TAG, "Audio scheduling: mic/AFE core=0, MultiNet core=1, frame=%d ms",
             s_afe->get_fetch_chunksize(s_afe_data) * 1000 / ASR_SAMPLE_RATE);
    ESP_LOGI(TAG, "Audio config: CPU=%d MHz, I-cache=%d KB, D-cache=%d KB, D-line=%d B",
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, CONFIG_ESP32S3_INSTRUCTION_CACHE_SIZE / 1024,
             CONFIG_ESP32S3_DATA_CACHE_SIZE / 1024, CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE);

    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    err = i2s_new_channel(&channel, NULL, &s_rx);
    if (err != ESP_OK) return err;
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_SCK,
            .ws = MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_SD,
        },
    };
    err = i2s_channel_init_std_mode(s_rx, &config);
    if (err == ESP_OK) err = i2s_channel_enable(s_rx);
    if (err == ESP_OK) {
        if (!s_lock) s_lock = xSemaphoreCreateMutex();
        if (!s_lock) err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        err = voice_live_init(live_event);
    }
    TaskHandle_t mic = NULL, wake = NULL;
    if (err == ESP_OK && xTaskCreatePinnedToCore(microphone_task, "hisu_mic", 4096, NULL, 5, &mic, 0) != pdPASS)
        err = ESP_ERR_NO_MEM;
    if (err == ESP_OK && xTaskCreatePinnedToCore(detection_task, "hisu_wake", 6144, NULL, 5, &wake, 1) != pdPASS)
        err = ESP_ERR_NO_MEM;
    if (err != ESP_OK) {
        if (wake) vTaskDelete(wake);
        if (mic) vTaskDelete(mic);
        voice_live_deinit();
        (void)i2s_channel_disable(s_rx);
        i2s_del_channel(s_rx);
        s_rx = NULL;
    }
    return err;
}
