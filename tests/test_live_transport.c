#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdio.h>
#include "live_transport_mock.h"
#undef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) ((ms) / 10)
#include "../components/hisu_voice/src/hisu_voice_live.c"

static input_t sent[32];
static output_t output[32];
static unsigned sent_count, output_count, events[LIVE_DEVICE+1];
static int device_settings[HISU_SETTING_COUNT];
static unsigned setting_writes;
static bool setting_error;
static hisu_network_status_t network_status;
static hisu_media_status_t media_status;
void hisu_network_get(hisu_network_status_t *out) { *out = network_status; }
void hisu_media_get(hisu_media_status_t *out) { *out = media_status; }
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
int hisu_settings_get(hisu_setting_t key) { return device_settings[key]; }
esp_err_t hisu_settings_set(hisu_setting_t key, int value) {
    ++setting_writes;
    if (setting_error) return ESP_ERR_INVALID_STATE;
    device_settings[key]=value; return ESP_OK;
}
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
    if (kind == LIVE_END) voice_live_close();
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
    memset(device_settings,0,sizeof(device_settings));
    device_settings[HISU_SETTING_VOLUME]=55;
    device_settings[HISU_SETTING_BRIGHTNESS]=75;
    device_settings[HISU_SETTING_SPEAKER]=1;
    setting_writes=0; setting_error=false;
    memset(&network_status,0,sizeof(network_status));
    memset(&media_status,0,sizeof(media_status));
    mock_reboots=0;
    mock_send_failure=false;
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
static cJSON *invoke_tool(const char *name, const char *args, bool *ended) {
    input_t frame={.epoch=s_epoch,.generation=s_generation,.kind=5};
    snprintf((char *)frame.pcm,sizeof(frame.pcm),"{\"id\":\"device-call\",\"name\":\"%s\",\"args\":%s}",name,args);
    char *wire=execute_tool(&frame,ended);
    assert(wire);
    cJSON *root=cJSON_Parse(wire); cJSON_free(wire); assert(root);
    cJSON *result=cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(root,"toolResponse"),"functionResponses"),0);
    assert(!strcmp(cJSON_GetObjectItem(result,"id")->valuestring,"device-call"));
    assert(!strcmp(cJSON_GetObjectItem(result,"name")->valuestring,name));
    cJSON *value=cJSON_Duplicate(cJSON_GetObjectItem(result,"response"),true);
    cJSON_Delete(root); assert(value); return value;
}
static void check_tool(const char *name, const char *args, bool success) {
    bool ended;
    cJSON *value=invoke_tool(name,args,&ended);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(value,"ok"))==success);
    assert(!ended); cJSON_Delete(value);
}
static void device_tool_tests(void) {
    reset();
    cJSON *setup=cJSON_Parse(voice_live_setup); assert(setup);
    cJSON *declarations=cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(setup,"setup"),"tools"),0),"functionDeclarations");
    assert(cJSON_GetArraySize(declarations)==14); cJSON_Delete(setup);
    bool ended;
    cJSON *value=invoke_tool("get_device_settings","{}",&ended);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(value,"ok")));
    assert(cJSON_GetObjectItem(value,"volume")->valueint==55);
    assert(!strcmp(cJSON_GetObjectItem(value,"theme")->valuestring,"dark"));
    assert(setting_writes==0 && !ended); cJSON_Delete(value);
    check_tool("set_volume","{\"percent\":35}",true);
    assert(device_settings[HISU_SETTING_VOLUME]==35 && events[LIVE_DEVICE]==1);
    check_tool("set_brightness","{\"percent\":80}",true);
    assert(device_settings[HISU_SETTING_BRIGHTNESS]==80);
    check_tool("set_theme","{\"theme\":\"light\"}",true);
    assert(device_settings[HISU_SETTING_THEME]==1);
    check_tool("set_theme","{\"theme\":\"dark\"}",true);
    assert(device_settings[HISU_SETTING_THEME]==0);
    check_tool("set_speaker","{\"enabled\":false}",true);
    assert(device_settings[HISU_SETTING_SPEAKER]==0);
    check_tool("set_screen_timeout","{\"seconds\":120}",true);
    assert(device_settings[HISU_SETTING_TIMEOUT]==120);
    check_tool("set_flashlight","{\"enabled\":true}",true);
    assert(events[LIVE_FLASHLIGHT]==1);
    unsigned before=setting_writes;
    check_tool("set_volume","{\"percent\":101}",false);
    check_tool("set_volume","{\"percent\":-1}",false);
    check_tool("set_volume","{\"percent\":3.5}",false);
    check_tool("set_brightness","{\"percent\":0}",false);
    check_tool("set_theme","{\"theme\":\"purple\"}",false);
    check_tool("set_speaker","{\"enabled\":1}",false);
    check_tool("set_screen_timeout","{\"seconds\":11}",false);
    check_tool("set_volume","{\"percent\":20,\"extra\":true}",false);
    check_tool("erase_device","{}",false);
    assert(setting_writes==before && voice_live_ready());
    setting_error=true;
    check_tool("set_volume","{\"percent\":60}",false);
    assert(device_settings[HISU_SETTING_VOLUME]==35);
    setting_error=false;
    value=invoke_tool("end_conversation","{}",&ended);
    assert(ended && cJSON_IsTrue(cJSON_GetObjectItem(value,"ok"))); cJSON_Delete(value);

    reset(); live_turn_start(&s_turn); live_turn_end(&s_turn);
    receive("{\"toolCall\":{\"functionCalls\":[{\"id\":\"a\",\"name\":\"set_volume\",\"args\":{\"percent\":20}}]}}");
    assert(sent_count==1 && sent[0].kind==5 && setting_writes==0);
    assert(run_tool((void *)1,&sent[0]));
    assert(device_settings[HISU_SETTING_VOLUME]==20);
    input_t stale=sent[0];
    assert(voice_live_interrupt(s_generation+1));
    before=setting_writes;
    assert(run_tool((void *)1,&stale) && setting_writes==before);
    assert(events[LIVE_ERROR]==0);
    input_t end={.epoch=s_epoch,.generation=s_generation,.kind=5};
    strcpy((char *)end.pcm,"{\"id\":\"end\",\"name\":\"end_conversation\",\"args\":{}}");
    assert(run_tool((void *)1,&end));
    assert(events[LIVE_END]==1 && !voice_live_ready());
}

static void extended_tool_tests(void) {
    reset();
    bool ended;
    network_status.connected=network_status.enabled=true;
    network_status.internet=HISU_INTERNET_CAPTIVE;
    strcpy(network_status.ssid,"Home"); strcpy(network_status.ip,"192.168.1.5");
    strcpy(network_status.setup_password,"must-not-leak");
    cJSON *value=invoke_tool("get_network_status","{}",&ended);
    assert(!strcmp(cJSON_GetObjectItem(value,"internet")->valuestring,"captive_portal"));
    assert(!strcmp(cJSON_GetObjectItem(value,"ssid")->valuestring,"Home"));
    char *wire=cJSON_PrintUnformatted(value); assert(!strstr(wire,"must-not-leak"));
    cJSON_free(wire); cJSON_Delete(value);
    network_status.connected=false;
    value=invoke_tool("get_network_status","{}",&ended);
    assert(!strcmp(cJSON_GetObjectItem(value,"internet")->valuestring,"disconnected"));
    assert(!strcmp(cJSON_GetObjectItem(value,"ssid")->valuestring,"")); cJSON_Delete(value);
    value=invoke_tool("get_system_info","{}",&ended);
    assert(!strcmp(cJSON_GetObjectItem(value,"firmware")->valuestring,"test-version"));
    assert(cJSON_GetObjectItem(value,"screen_width")->valueint==240); cJSON_Delete(value);
    check_tool("get_network_status","{\"extra\":true}",false);
    check_tool("set_wallpaper","{\"source\":\"uploaded\"}",false);
    assert(setting_writes==0);
    check_tool("set_wallpaper","{\"source\":\"default\"}",true);
    assert(device_settings[HISU_SETTING_WALLPAPER_DEFAULT]==1);
    media_status.has_image=media_status.gif=true;
    check_tool("set_wallpaper","{\"source\":\"uploaded\"}",true);
    assert(device_settings[HISU_SETTING_WALLPAPER_DEFAULT]==0 && media_status.has_image);
    value=invoke_tool("get_wallpaper","{}",&ended);
    assert(!strcmp(cJSON_GetObjectItem(value,"source")->valuestring,"uploaded")); cJSON_Delete(value);
    check_tool("set_wallpaper","{\"source\":\"arbitrary-url\"}",false);
    check_tool("confirm_reboot","{}",false);
    check_tool("request_reboot","{}",true);
    strcpy(s_confirmation,"Xác nhận khởi động lại.");
    check_tool("confirm_reboot","{}",false); /* Same turn cannot confirm itself. */
    assert(voice_live_begin(11));
    check_tool("confirm_reboot","{}",false);
    live_turn_start(&s_turn); live_turn_end(&s_turn);
    receive("{\"serverContent\":{\"inputTranscription\":{\"text\":\"Đừng xác nhận khởi động lại\"}}}");
    check_tool("confirm_reboot","{}",false);
    s_confirmation[0]=0;
    receive("{\"serverContent\":{\"inputTranscription\":{\"text\":\"Xác nhận khởi động lại.\"}}}");
    s_reboot_deadline=-1;
    check_tool("confirm_reboot","{}",false);
    s_reboot_deadline=INT64_MAX;
    input_t confirm={.epoch=s_epoch,.generation=s_generation,.kind=5};
    strcpy((char *)confirm.pcm,"{\"id\":\"reboot\",\"name\":\"confirm_reboot\",\"args\":{}}");
    assert(run_tool((void *)1,&confirm));
    assert(mock_reboots==1 && !voice_live_ready() && events[LIVE_END]==1);
    reset(); check_tool("request_reboot","{}",true);
    voice_live_close(); voice_live_open(11); s_ready=true;
    strcpy(s_confirmation,"xac nhan khoi dong lai");
    check_tool("confirm_reboot","{}",false); /* A new session invalidates pending reboot. */
    check_tool("request_reboot","{}",true);
    assert(voice_live_begin(12));
    strcpy(s_confirmation,"xac nhan khoi dong lai");
    confirm.epoch=s_epoch; confirm.generation=s_generation;
    mock_send_failure=true;
    assert(!run_tool((void *)1,&confirm) && mock_reboots==0);
    reset(); check_tool("request_reboot","{}",true);
    assert(voice_live_begin(11));
    strcpy(s_confirmation,"xac nhan khoi dong lai");
    confirm.epoch=s_epoch; confirm.generation=s_generation;
    assert(voice_live_interrupt(12));
    assert(run_tool((void *)1,&confirm) && mock_reboots==0); /* Stale queued confirmation. */
}

int main(void) {
    device_tool_tests();
    extended_tool_tests();
    assert(speaker_open() == ESP_OK);
    assert(mock_i2s_channel_config.auto_clear_after_cb);
    speaker_close();
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
