#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdio.h>
#include "live_transport_mock.h"
#undef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) ((ms) / 10)
#include "../components/hisu_voice/src/hisu_voice_live.c"

static input_t sent[32];
static output_t output[32];
static unsigned sent_count, output_count, events[LIVE_MUSIC_ERROR+1];
static uint32_t music_ticket;
static bool music_busy, music_error;
static unsigned music_resolve_calls;
static bool music_emit_pcm, full_output;
bool hisu_music_current(uint32_t ticket) { return music_busy && ticket==music_ticket; }
bool hisu_music_cancel(void) { bool busy=music_busy; ++music_ticket; music_busy=false; return busy; }
void hisu_music_finish(uint32_t ticket) { if (ticket==music_ticket) music_busy=false; }
esp_err_t hisu_music_search(const char *query, cJSON *results) { return music_error ? ESP_ERR_INVALID_STATE : ESP_OK; }
esp_err_t hisu_music_queue(const char *id, hisu_music_track_t *track) {
    if (music_error) return ESP_ERR_INVALID_STATE;
    track->ticket=++music_ticket; music_busy=true;
    strcpy(track->title,"Test song"); strcpy(track->artist,"Test artist");
    strcpy(track->url,"https://a128-zmp3.zmdcdn.me/test.mp3"); return ESP_OK;
}
esp_err_t hisu_music_resolve(hisu_music_track_t *track) {
    ++music_resolve_calls;
    return music_error ? ESP_ERR_INVALID_STATE : ESP_OK;
}
esp_err_t hisu_music_play(const hisu_music_track_t *track, hisu_music_pcm_fn output) {
    static const int16_t pcm[4800]={1,2,3,4};
    if (music_emit_pcm)
        for (int i=0;i<3;++i) if (!output(pcm,4800,24000,2)) return ESP_FAIL;
    return ESP_OK;
}
static int device_settings[HISU_SETTING_COUNT];
static unsigned setting_writes;
static bool setting_error;
static hisu_network_status_t network_status;
static hisu_media_status_t media_status;
void hisu_network_get(hisu_network_status_t *out) { *out = network_status; }
void hisu_media_get(hisu_media_status_t *out) { *out = media_status; }
static uint32_t last_generation;
static bool full_input, interrupt_on_text, error_has_text;
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
        if (full_output) return pdFALSE;
        assert(queue == s_output && output_count < 32);
        output[output_count++] = *(const output_t *)data;
    }
    return pdTRUE;
}
static void event(live_event_t kind, uint32_t generation, const char *text) {
    ++events[kind]; last_generation = generation;
    if (kind == LIVE_READY) assert(text && !text[0]);
    if (kind == LIVE_ERROR) error_has_text = text && text[0];
    if (kind == LIVE_END) voice_live_close();
    if (kind == LIVE_TEXT && interrupt_on_text) {
        interrupt_on_text = false;
        assert(voice_live_interrupt(generation + 1));
    }
}
static void reset(void) {
    for (unsigned i = 0; i < output_count; ++i) free(output[i].pcm);
    output_count = sent_count = 0; full_input = interrupt_on_text = false;
    free(s_pending_music); s_pending_music=NULL; music_error=false;
    music_resolve_calls=0;
    music_emit_pcm=full_output=false; mock_delay_hook=NULL;
    s_music_playing=s_music_producer_done=false;
    s_music_queued_bytes=0; s_music_queued_us=0;
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
    mock_short_send=false;
    mock_text_sends=0; mock_last_text[0]=0;
    mock_on_send=NULL;
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
    cJSON *config=cJSON_GetObjectItem(cJSON_GetObjectItem(setup,"setup"),"generationConfig");
    cJSON *instruction=cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(setup,"setup"),"systemInstruction"),"parts"),0),"text");
    assert(strstr(instruction->valuestring,"A name-only greeting must include audible speech"));
    assert(!strncmp(instruction->valuestring,"Bạn là Hi Su",strlen("Bạn là Hi Su")));
    assert(strstr(instruction->valuestring,"Ngôn ngữ trò chuyện mặc định là tiếng Việt"));
    assert(strstr(instruction->valuestring,"trừ khi người dùng chủ động yêu cầu rõ ràng"));
    assert(strstr(instruction->valuestring,"Không tự đổi ngôn ngữ theo tên bài hát"));
    assert(strstr(instruction->valuestring,"do not sing, imitate a song or invent lyrics"));
    assert(!cJSON_GetObjectItem(cJSON_GetObjectItem(config,"speechConfig"),"languageCode"));
    assert(strstr(instruction->valuestring,"Do not mention the tool or claim to have real human feelings"));
    cJSON *voice=cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(config,"speechConfig"),"voiceConfig"),"prebuiltVoiceConfig");
    cJSON *voice_name=cJSON_GetObjectItem(voice,"voiceName");
    assert(cJSON_IsString(voice_name) && !strcmp(voice_name->valuestring,"Leda"));
    cJSON *modalities=cJSON_GetObjectItem(config,"responseModalities");
    assert(cJSON_GetArraySize(modalities)==1 &&
        !strcmp(cJSON_GetArrayItem(modalities,0)->valuestring,"AUDIO"));
    cJSON *declarations=cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(setup,"setup"),"tools"),0),"functionDeclarations");
    assert(cJSON_GetArraySize(declarations)==18); cJSON_Delete(setup);
    check_tool("set_expression","{\"expression\":\"happy\"}",true);
    assert(output_count == 1 && output[0].cue == 2 && events[LIVE_EXPRESSION] == 0);
    play_cue(&output[0]);
    assert(events[LIVE_EXPRESSION] == 1);
    check_tool("set_expression","{\"expression\":\"invalid\"}",false);
    check_tool("set_expression","{\"expression\":\"sad\",\"extra\":1}",false);
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

static void wake_reply_tests(void) {
    reset(); s_ready=false;
    receive("{\"setupComplete\":{}}");
    assert(events[LIVE_READY]==1 && sent_count==1 && sent[0].kind==6);
    receive("{\"setupComplete\":{}}");
    assert(events[LIVE_READY]==1 && sent_count==1); /* No duplicate greetings. */
    assert(send_wake((void *)1,&sent[0]) && s_turn.response_pending);
    assert(mock_text_sends==1 && live_turn_accept_output(&s_turn));
    cJSON *wire=cJSON_Parse(mock_last_text); assert(wire);
    cJSON *content=cJSON_GetObjectItem(wire,"clientContent");
    assert(cJSON_IsTrue(cJSON_GetObjectItem(content,"turnComplete")));
    cJSON *turn=cJSON_GetArrayItem(cJSON_GetObjectItem(content,"turns"),0);
    assert(!strcmp(cJSON_GetObjectItem(turn,"role")->valuestring,"user"));
    cJSON *part=cJSON_GetArrayItem(cJSON_GetObjectItem(turn,"parts"),0);
    assert(!strcmp(cJSON_GetObjectItem(part,"text")->valuestring,"Hi Su")); cJSON_Delete(wire);
    receive(audio_done);
    assert(output_count==3 && output[0].cue==1 && output[2].done && events[LIVE_TEXT]==0);
    playback_caption_t caption={0};
    hold_caption(&caption,&output[0]); assert(events[LIVE_TEXT]==0);
    flush_caption(&caption,output[1].epoch,output[1].generation);
    assert(events[LIVE_TEXT]==1 && !caption.text[0]);
    assert(!s_turn.response_pending); /* Normal playback completion opens listening again. */

    reset(); s_ready=false;
    assert(voice_live_interrupt(11));
    receive("{\"setupComplete\":{}}");
    assert(sent_count==1 && sent[0].generation==11);
    assert(send_wake((void *)1,&sent[0]) && !s_turn.restart_pending);
    receive(audio_done);
    assert(output_count==3 && output[2].generation==11);

    reset(); s_ready=false; receive("{\"setupComplete\":{}}");
    input_t wake=sent[0];
    assert(voice_live_interrupt(11));
    assert(send_wake((void *)1,&wake) && mock_text_sends==0); /* Superseded before send. */
    reset(); s_ready=false; receive("{\"setupComplete\":{}}");
    wake=sent[0]; voice_live_close();
    assert(send_wake((void *)1,&wake) && mock_text_sends==0);

    reset(); s_ready=false; receive("{\"setupComplete\":{}}");
    assert(send_wake((void *)1,&sent[0]));
    assert(voice_live_interrupt(11) && s_turn.obsolete_turns==1);
    receive(audio_done);
    assert(output_count==0 && events[LIVE_TEXT]==0); /* Interrupted greeting is discarded. */

    reset(); s_ready=false; full_input=true;
    receive("{\"setupComplete\":{}}");
    assert(events[LIVE_ERROR]==1 && !voice_live_ready());
    reset(); s_ready=false; receive("{\"setupComplete\":{}}");
    mock_send_failure=true;
    assert(!send_wake((void *)1,&sent[0])); /* Caller fails the session on send error. */
}

static void music_tests(void) {
    reset();
    check_tool("search_music","{\"query\":\"Lac Troi Son Tung\"}",true);
    check_tool("search_music","{\"query\":\"\"}",false);
    check_tool("search_music","{\"query\":\"test\",\"extra\":true}",false);
    check_tool("play_music","{\"id\":\"../unsafe\"}",false);
    check_tool("play_music","{\"id\":\"https://example.com\"}",false);
    music_error=true;
    check_tool("play_music","{\"id\":\"qYD31k5TVTCQ\"}",false);
    assert(!s_pending_music && voice_live_ready());
    music_error=false;
    input_t play={.epoch=s_epoch,.generation=s_generation,.kind=5};
    strcpy((char *)play.pcm,"{\"id\":\"song\",\"name\":\"play_music\",\"args\":{\"id\":\"qYD31k5TVTCQ\"}}");
    assert(run_tool((void *)1,&play));
    assert(!voice_live_ready() && s_pending_music && events[LIVE_END]==1);
    assert(music_busy); /* Acknowledged, but not played until transport teardown. */
    uint32_t ticket=s_pending_music->ticket;
    voice_live_open(20);
    assert(!hisu_music_current(ticket) && !music_busy);
    reset();
    play.epoch=s_epoch; play.generation=s_generation; mock_send_failure=true;
    assert(!run_tool((void *)1,&play));
    assert(!s_pending_music && !music_busy); /* No playback after failed acknowledgment. */

    assert(hisu_music_valid_id("qYD31k5TVTCQ"));
    assert(!hisu_music_valid_id("id?other=1"));
    assert(hisu_music_valid_url("https://a128-zmp3.zmdcdn.me/path/file.mp3?token=abc"));
    assert(!hisu_music_valid_url("http://a128-zmp3.zmdcdn.me/a.mp3"));
    assert(!hisu_music_valid_url("https://a.zmdcdn.me.evil.test/a.mp3"));
    assert(!hisu_music_valid_url("https://user@a.zmdcdn.me/a.mp3"));
    assert(!hisu_music_valid_url("https://a.zmdcdn.me:443/a.mp3"));
    assert(!hisu_music_valid_url("https://127.0.0.1/a.mp3"));
    char encoded[64];
    assert(hisu_music_encode_query("Lạc Trôi &",encoded,sizeof(encoded)));
    assert(!strcmp(encoded,"L%E1%BA%A1c%20Tr%C3%B4i%20%26"));
    assert(!hisu_music_encode_query("too long",encoded,3));
    cJSON *root=cJSON_Parse("{\"err\":0,\"data\":{\"songs\":[{\"encodeId\":\"song1\",\"title\":\"Test\",\"artistsNames\":\"Artist\"},{\"encodeId\":\"../bad\",\"title\":\"Bad\",\"artistsNames\":\"Other\"}]}}");
    cJSON *songs=cJSON_CreateArray();
    assert(hisu_music_parse_search(root,songs) && cJSON_GetArraySize(songs)==1);
    cJSON_Delete(songs); cJSON_Delete(root);
    root=cJSON_Parse("{\"err\":-201,\"data\":{}}"); songs=cJSON_CreateArray();
    assert(!hisu_music_parse_search(root,songs)); cJSON_Delete(root); cJSON_Delete(songs);
    cJSON *info=cJSON_Parse("{\"err\":0,\"data\":{\"title\":\"Test\",\"artistsNames\":\"Artist\"}}");
    cJSON *stream=cJSON_Parse("{\"err\":0,\"data\":{\"128\":\"VIP\"}}");
    hisu_music_track_t track={0};
    assert(!hisu_music_parse_track(info,stream,&track));
    cJSON_Delete(stream);
    stream=cJSON_Parse("{\"err\":0,\"data\":{\"128\":\"https://a.zmdcdn.me/song.mp3\"}}");
    assert(hisu_music_parse_track(info,stream,&track) && !strcmp(track.title,"Test"));
    cJSON_Delete(info); cJSON_Delete(stream);
}

static void socket_transport_tests(void) {
    reset();
    connection_t connection={.epoch=s_epoch};
    char headers[]="x-goog-api-key: private-test-key\r\n";
    assert(create_connection_transport(&connection,headers));
    assert(mock_transport_alive==2 && connection.tls->bundle);
    assert(connection.ws->parent==connection.tls && connection.ws->port==443 && connection.ws->control);
    assert(!strcmp(connection.ws->path,"/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent"));
    memset(headers,0,sizeof(headers));
    assert(strstr(connection.ws->headers,"private-test-key")); /* Config owns a copy. */
    websocket_event(&connection,0,WEBSOCKET_EVENT_CONNECTED,NULL);
    assert(mock_nodelay_calls==1 && connection.connected);
    mock_nodelay_result=-1;
    websocket_event(&connection,0,WEBSOCKET_EVENT_CONNECTED,NULL);
    assert(mock_nodelay_calls==2 && voice_live_ready()); /* Optimization failure is nonfatal. */
    mock_nodelay_result=0;
    destroy_connection_transport(&connection); destroy_connection_transport(&connection);
    assert(!connection.ws && !connection.tls && !mock_transport_alive);
    for (int failure=1; failure<=3; ++failure) {
        mock_transport_failure=failure;
        assert(!create_connection_transport(&connection,"test"));
        assert(!mock_transport_alive && !connection.ws && !connection.tls);
    }
    mock_transport_failure=0;
    assert(send_text((void *)1,"test"));
    assert(mock_send_timeout==500); /* 5000 ms converted to 10 ms RTOS ticks. */
    unsigned sends=mock_text_sends;
    mock_send_failure=true;
    assert(!send_text((void *)1,"test") && mock_text_sends==sends+1);
    mock_send_failure=false; mock_short_send=true; sends=mock_text_sends;
    assert(!send_text((void *)1,"test") && mock_text_sends==sends+1); /* Never replay a partial frame. */
    mock_short_send=false;
    assert(SEND_BODY_CAPACITY<=WSS_BUFFER_BYTES);
}

static void reply_during_send(void) { receive(audio_done); }

static void spoken_reply_tests(void) {
    static const char *expression_call = "{\"toolCall\":{\"functionCalls\":[{\"id\":\"eyes\",\"name\":\"set_expression\",\"args\":{\"expression\":\"happy\"}}]}}";
    static const char *complete = "{\"serverContent\":{\"turnComplete\":true}}";
    static const char *search_call = "{\"toolCall\":{\"functionCalls\":[{\"id\":\"search\",\"name\":\"search_music\",\"args\":{\"query\":\"Song Gio\"}}]}}";
    static const char *play_call = "{\"toolCall\":{\"functionCalls\":[{\"id\":\"play\",\"name\":\"play_music\",\"args\":{\"id\":\"song1\"}}]}}";
    reset(); live_turn_end(&s_turn); receive(search_call); receive(audio_done);
    assert(s_reply_state.pending_tools==1 && s_reply_state.deferred_complete && output_count==2);
    assert(run_tool((void *)1,&sent[0]));
    assert(s_turn.response_pending && !s_reply_state.deferred_complete && output_count==2);
    receive(play_call);
    assert(sent_count==2 && s_reply_state.pending_tools==1);
    assert(run_tool((void *)1,&sent[1]) && s_pending_music && events[LIVE_END]==1);
    assert(music_resolve_calls==0); /* No music HTTPS while acknowledging the Gemini tool. */
    assert(events[LIVE_DONE]==0 && !voice_live_ready()); /* Music follow-up survives a pre-result audio boundary. */

    reset(); live_turn_end(&s_turn); receive(search_call); receive(complete);
    mock_on_send=reply_during_send;
    assert(run_tool((void *)1,&sent[0]));
    assert(!s_turn.response_pending && output_count==3 && output[2].done && sent_count==1);
    assert(!s_reply_state.pending_tools && !s_reply_state.retry_queued); /* Fresh completion during result send is not lost. */

    reset(); live_turn_end(&s_turn);
    receive(expression_call); assert(s_reply_state.pending_tools==1 && sent_count==1);
    receive(complete);
    assert(s_reply_state.deferred_complete && s_turn.response_pending && output_count==0);
    assert(run_tool((void *)1,&sent[0]));
    assert(s_reply_state.pending_tools==0 && sent_count==1 && s_turn.response_pending);
    assert(output_count==1 && output[0].cue==2 && events[LIVE_DONE]==0);
    receive(complete); assert(sent_count==2 && !s_failed); /* A fresh silent continuation gets one recovery. */
    assert(send_spoken_continuation((void *)1,&sent[1]) && s_reply_state.retry_sent);
    cJSON *wire=cJSON_Parse(mock_last_text); assert(wire);
    assert(cJSON_GetObjectItem(wire,"clientContent")); cJSON_Delete(wire);
    assert(strstr(mock_last_text,"Hãy nói tiếp câu trả lời trước bằng tiếng Việt"));
    receive(audio_done);
    assert(output_count==4 && output[3].done && !s_turn.response_pending);

    reset(); live_turn_end(&s_turn); receive(expression_call);
    assert(run_tool((void *)1,&sent[0])); receive(audio_done);
    assert(output_count==4 && output[3].done && sent_count==1); /* Normal tool-then-speech is unchanged. */

    reset(); live_turn_end(&s_turn);
    receive("{\"toolCall\":{\"functionCalls\":[{\"id\":\"a\",\"name\":\"get_system_info\",\"args\":{}},{\"id\":\"b\",\"name\":\"set_expression\",\"args\":{\"expression\":\"happy\"}}]}}");
    receive(complete);
    assert(s_reply_state.pending_tools==2 && sent_count==2);
    assert(run_tool((void *)1,&sent[0]) && s_reply_state.pending_tools==1 && sent_count==2);
    assert(run_tool((void *)1,&sent[1]) && s_reply_state.pending_tools==0 && sent_count==2);
    assert(s_turn.response_pending && !s_reply_state.retry_queued);
    receive(complete); assert(sent_count==3);
    assert(sent[2].kind==7); /* Only after every result has been acknowledged. */

    reset(); live_turn_end(&s_turn); receive(expression_call);
    assert(run_tool((void *)1,&sent[0])); receive(complete);
    input_t retry=sent[1]; unsigned sends=mock_text_sends;
    assert(voice_live_interrupt(11) && s_turn.obsolete_turns==0);
    assert(send_spoken_continuation((void *)1,&retry) && mock_text_sends==sends);
    live_turn_start(&s_turn); live_turn_end(&s_turn); receive(audio_done);
    assert(output_count==4 && output[3].done && output[3].generation==11);

    reset(); live_turn_end(&s_turn); receive(expression_call); receive(audio_done);
    assert(output_count==2 && s_reply_state.deferred_complete && s_reply_state.audio);
    assert(run_tool((void *)1,&sent[0]));
    assert(output_count==3 && s_turn.response_pending && !s_reply_state.retry_queued);
    receive(complete);
    assert(output_count==4 && output[3].done && sent_count==1); /* No retry if audio already arrived. */

    reset(); live_turn_end(&s_turn); receive(complete); retry=sent[0];
    receive(audio_done); sends=mock_text_sends;
    assert(send_spoken_continuation((void *)1,&retry) && mock_text_sends==sends);
    assert(output_count==3 && output[2].done); /* Late follow-up wins over queued retry. */

    reset(); live_turn_end(&s_turn); receive(complete);
    assert(send_spoken_continuation((void *)1,&sent[0])); receive(complete);
    assert(events[LIVE_ERROR]==1 && !error_has_text && s_failed && output_count==0 && sent_count==1);

    reset(); live_turn_end(&s_turn); receive(complete);
    assert(send_spoken_continuation((void *)1,&sent[0]));
    receive(expression_call);
    assert(events[LIVE_ERROR]==1 && !error_has_text && sent_count==1 && output_count==0); /* Recovery cannot repeat tools. */

    reset(); live_turn_end(&s_turn);
    receive("{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"inlineData\":{\"mimeType\":\"audio/pcm;rate=24000\",\"data\":\"AAAAAA==\"}}]},\"turnComplete\":true,\"interactionStatus\":\"IN_PROGRESS\"}}");
    assert(output_count==1 && !output[0].done && s_turn.response_pending);
    receive("{\"serverContent\":{\"turnComplete\":true,\"interactionStatus\":\"IDLE\"}}");
    assert(output_count==2 && output[1].done && !s_turn.response_pending);

    reset(); live_turn_end(&s_turn); full_input=true; receive(complete);
    assert(events[LIVE_ERROR]==1 && output_count==0); /* Cannot queue recovery. */

    reset(); live_turn_end(&s_turn); mock_live_alloc_left=0; receive(complete);
    assert(events[LIVE_ERROR]==1 && sent_count==0); mock_live_alloc_left=-1;
    reset(); live_turn_end(&s_turn); receive(complete);
    mock_send_failure=true;
    assert(!send_spoken_continuation((void *)1,&sent[0])); /* Caller handles transport failure. */
    reset(); live_turn_end(&s_turn);
    uint32_t old_generation=s_generation;
    assert(voice_live_interrupt(old_generation+1));
    fail_with_message(s_epoch,old_generation,"stale silent reply",true);
    assert(events[LIVE_ERROR]==0 && !s_failed); /* Old semantic errors cannot end the new turn. */
}

static void send_workspace_tests(void) {
    reset();
    for (int i=0;i<5;++i) {
        mock_live_alloc_left=i;
        assert(voice_live_init(event)==ESP_ERR_NO_MEM);
        assert(!s_send_workspace && !s_send_storage && !s_input_storage && !s_output_storage);
        assert(!s_speaker_mix);
        assert(!s_input && !s_output);
    }
    mock_live_alloc_left=-1;
    assert(voice_live_init(event)==ESP_OK && s_send_workspace);
    assert(&s_send_workspace->frame != &s_send_workspace->peek);
    assert(sizeof(s_send_workspace->key)==128 && sizeof(s_send_workspace->headers)==160);
    voice_live_deinit(); voice_live_deinit();
    assert(!s_speaker && !s_speaker_enabled);
    assert(!s_send_workspace && !s_send_storage && !s_input_storage && !s_output_storage);
}

static hisu_music_track_t *queued_music(void) {
    hisu_music_track_t *track=calloc(1,sizeof(*track)); assert(track);
    assert(hisu_music_queue("song1",track)==ESP_OK);
    return track;
}
static void cancel_music_on_delay(void) { hisu_music_cancel(); }
static void fill_music_on_delay(void) { s_music_queued_us=500000; }
static void consume_music_queue(void) {
    for (unsigned i=0;i<output_count;++i) {
        consume_music(&output[i]); output[i].pcm=NULL;
    }
}
static void audio_buffer_tests(void) {
    assert(voice_live_init(event)==ESP_OK && s_speaker_mix);
    reset(); live_turn_end(&s_turn); mock_now=1000000;
    const char *chunk="{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"inlineData\":{\"mimeType\":\"audio/pcm;rate=24000\",\"data\":\"AAAAAA==\"}}]}}}";
    receive(chunk); mock_now=2100000; receive(chunk);
    assert(s_reply_state.rx_bytes==8 && s_reply_state.rx_first_us==1000000 &&
        s_reply_state.rx_last_us==2100000 && s_reply_state.max_rx_gap_us==1100000);
    assert(voice_live_interrupt(s_generation+1) && !s_reply_state.rx_bytes && !s_reply_state.max_rx_gap_us);
    reset(); live_turn_end(&s_turn); mock_now=0;
    assert(wait_audio_buffer(false,true,s_epoch,s_generation,100,0) && mock_now==750000);
    s_queued_audio_bytes=23899; mock_now=0;
    assert(wait_audio_buffer(false,true,s_epoch,s_generation,100,0) && mock_now==750000);
    s_queued_audio_bytes=23900; mock_now=0;
    assert(wait_audio_buffer(false,true,s_epoch,s_generation,100,0) && !mock_now);
    s_queued_audio_bytes=2779; mock_now=0;
    assert(wait_audio_buffer(false,false,s_epoch,s_generation,100,0) && mock_now==80000);
    s_queued_audio_bytes=2780; mock_now=0;
    assert(wait_audio_buffer(false,false,s_epoch,s_generation,100,0) && !mock_now);
    s_queued_audio_bytes=0; s_turn.response_pending=false;
    assert(wait_audio_buffer(false,true,s_epoch,s_generation,100,0) && !mock_now);
    live_turn_end(&s_turn); mock_delay_hook=voice_live_close;
    assert(!wait_audio_buffer(false,true,s_epoch,s_generation,100,0));

    reset(); hisu_music_track_t *track=queued_music();
    s_music_producer_done=false; mock_now=0; mock_delay_hook=fill_music_on_delay;
    assert(wait_audio_buffer(true,true,0,track->ticket,100,0) && mock_now==10000);
    s_music_queued_us=59999; mock_now=0;
    assert(wait_audio_buffer(true,false,0,track->ticket,100,0) && mock_now==80000);
    s_music_queued_us=60000; mock_now=0;
    assert(wait_audio_buffer(true,false,0,track->ticket,100,0) && !mock_now);
    s_music_queued_us=0; s_music_producer_done=true; mock_now=0;
    assert(wait_audio_buffer(true,true,0,track->ticket,100,0) && !mock_now);
    hisu_music_finish(track->ticket); free(track);

    reset(); voice_live_close(); track=queued_music(); music_emit_pcm=true;
    size_t written=mock_i2s_written_bytes;
    produce_music(track);
    assert(output_count==4 && s_music_playing && s_music_producer_done && s_music_queued_bytes==28800);
    assert(mock_i2s_written_bytes==written && music_resolve_calls==1); /* Producer never writes I2S. */
    consume_music_queue();
    assert(mock_i2s_written_bytes-written==28800 && !s_music_queued_bytes && !s_music_queued_us);
    assert(finish_music_output() && !s_music_output && !s_music_playing && !music_busy);
    assert(events[LIVE_MUSIC]==2 && !events[LIVE_MUSIC_ERROR]);

    reset(); voice_live_close(); track=queued_music(); music_emit_pcm=true;
    produce_music(track); hisu_music_cancel(); written=mock_i2s_written_bytes;
    consume_music_queue();
    assert(finish_music_output() && mock_i2s_written_bytes==written && !events[LIVE_MUSIC_ERROR]);

    reset(); voice_live_close(); track=queued_music(); music_emit_pcm=true;
    produce_music(track); mock_i2s_short_write=true;
    consume_music_queue(); mock_i2s_short_write=false;
    assert(finish_music_output() && events[LIVE_MUSIC_ERROR]==1 && !music_busy);

    reset(); voice_live_close(); track=queued_music();
    s_music_output=track; s_music_playing=true; s_music_producer_done=false;
    assert(!finish_music_output() && s_music_output==track && music_busy);
    s_music_producer_done=true; s_music_result=ESP_ERR_INVALID_STATE;
    assert(finish_music_output() && !s_music_output && !s_music_playing);

    reset(); voice_live_close(); track=queued_music(); music_error=true;
    produce_music(track); assert(output_count==1 && s_music_producer_done);
    consume_music_queue();
    assert(finish_music_output() && events[LIVE_MUSIC_ERROR]==1 && !s_music_playing);

    reset(); voice_live_close(); track=queued_music(); music_emit_pcm=true; mock_live_alloc_left=0;
    produce_music(track); mock_live_alloc_left=-1;
    consume_music_queue();
    assert(finish_music_output() && events[LIVE_MUSIC_ERROR]==1 && !s_music_playing);

    reset(); voice_live_close(); track=queued_music(); full_output=true;
    produce_music(track);
    assert(!s_music_playing && !music_busy && !output_count && events[LIVE_MUSIC_ERROR]==1);

    reset(); track=queued_music(); s_music_producer_ticket=track->ticket;
    const int16_t pcm[4]={1,2,3,4}; full_output=true; mock_delay_hook=cancel_music_on_delay;
    assert(!music_enqueue(pcm,4,24000,2) && !s_music_queued_bytes && !s_music_queued_us && !output_count);
    free(track);
    reset(); track=queued_music(); s_music_producer_ticket=track->ticket;
    s_music_queued_bytes=128*1024; s_music_queued_us=1000000; mock_delay_hook=cancel_music_on_delay;
    assert(!music_enqueue(pcm,4,24000,2) && s_music_queued_bytes==128*1024 && !output_count);
    free(track); reset(); voice_live_deinit();
}

int main(void) {
    device_tool_tests();
    extended_tool_tests();
    wake_reply_tests();
    music_tests();
    socket_transport_tests();
    send_workspace_tests();
    spoken_reply_tests();
    audio_buffer_tests();
    assert(speaker_open() == ESP_OK);
    assert(mock_i2s_channel_config.auto_clear_after_cb);
    assert(mock_i2s_channel_config.dma_desc_num==4 && mock_i2s_channel_config.dma_frame_num==160);
    unsigned created=mock_i2s_created, preloaded=mock_i2s_preloaded, reconfigured=mock_i2s_reconfigured;
    speaker_close();
    assert(s_speaker && !s_speaker_enabled);
    assert(speaker_open_rate(44100)==ESP_OK && s_speaker_enabled && s_speaker_rate==44100);
    assert(mock_i2s_created==created && mock_i2s_preloaded==preloaded+1 && mock_i2s_reconfigured==reconfigured+1);
    speaker_close();
    mock_i2s_clock_error=ESP_FAIL;
    assert(speaker_open()==ESP_FAIL && !s_speaker_enabled && s_speaker_rate==44100);
    mock_i2s_clock_error=ESP_OK;
    assert(speaker_open()==ESP_OK && s_speaker_rate==24000 && mock_i2s_created==created);
    speaker_release();
    unsigned deleted=mock_i2s_deleted;
    mock_i2s_init_error=ESP_ERR_NO_MEM;
    assert(speaker_open()==ESP_ERR_NO_MEM && !s_speaker && mock_i2s_deleted==deleted+1);
    mock_i2s_init_error=ESP_OK;
    assert(speaker_open()==ESP_OK);
    speaker_close();
    mock_i2s_enable_error=ESP_FAIL;
    assert(speaker_open()==ESP_FAIL && !s_speaker && !s_speaker_enabled);
    mock_i2s_enable_error=ESP_OK;
    assert(speaker_open()==ESP_OK);
    speaker_release();
    int16_t stereo[512] = {0};
    s_speaker = (void *)1;
    s_speaker_enabled = true;
    assert(speaker_write(stereo,sizeof(stereo)));
    assert(mock_i2s_write_timeout == 100);
    mock_i2s_short_write = true;
    assert(!speaker_write(stereo,sizeof(stereo)));
    mock_i2s_short_write = false;
    s_speaker = NULL;
    s_speaker_enabled = false;
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
    input_t next;
    assert(collect_audio(&first,batch,&next) == 1280 && read_index == 1);
    assert(memcmp(batch,first.pcm,sizeof(first.pcm)) == 0);
    assert(memcmp(batch+640,first.pcm,sizeof(first.pcm)) == 0);
    reset(); sent[0] = first; sent[0].kind = 2; sent_count = 1;
    assert(collect_audio(&first,batch,&next) == 640 && read_index == 0);
    reset(); sent[0] = first; ++sent[0].generation; sent_count = 1;
    assert(collect_audio(&first,batch,&next) == 640 && read_index == 0);
    reset(); sent[0] = first; ++sent[0].epoch; sent_count = 1;
    assert(collect_audio(&first,batch,&next) == 640 && read_index == 0);
    reset(); first.count = 512; sent[0] = first; sent[1] = first; sent[2] = first; sent_count = 3;
    assert(collect_audio(&first,batch,&next) == 1536 && read_index == 2);
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
    assert(events[LIVE_TEXT] == 0);
    assert(output_count == 3 && output[0].generation == 12 && output[2].done);
    play_cue(&output[0]); assert(events[LIVE_TEXT] == 1 && last_generation == 12);
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
    receive(audio_done);
    assert(events[LIVE_TEXT] == 0); /* Network reception must not advance subtitles. */
    play_cue(&output[0]); /* Interruption races with playback of the final old response. */
    assert(s_generation == 11 && s_turn.obsolete_turns == 0);
    assert(output_count == 3 && !current(output[0].epoch, output[0].generation));
    play_cue(&output[0]); assert(events[LIVE_TEXT] == 1); /* Stale cue is ignored. */

    char caption[16] = "";
    voice_caption_append(caption, sizeof(caption), "Xin chào bạn");
    assert(!strcmp(caption, "Xin chào bạn"));
    for (int i = 0; i < 20; ++i) voice_caption_append(caption, sizeof(caption), " tiếng Việt");
    assert(strlen(caption) < sizeof(caption) && ((unsigned char)caption[0] & 0xc0) != 0x80);
    assert(strstr(caption, "Việt")); /* Long replies keep updating, preserving UTF-8. */

    reset(); live_turn_start(&s_turn); live_turn_end(&s_turn); receive(audio_done);
    playback_caption_t pending={0}; hold_caption(&pending,&output[0]);
    assert(events[LIVE_TEXT]==0 && pending.text[0]);
    voice_live_interrupt(11);
    flush_caption(&pending,output[1].epoch,output[1].generation);
    assert(events[LIVE_TEXT]==0 && !pending.text[0]); /* No old subtitle after interruption. */

    reset(); fail(s_epoch, 9);
    assert(events[LIVE_ERROR] == 1 && last_generation == 10);
    reset(); voice_live_close(); epoch = s_epoch;
    fail(epoch - 1, 10);
    assert(events[LIVE_ERROR] == 0);
    reset();
    puts("Live transport tests passed: wake replies, same connection on interrupt, old-response fencing, setup interruption, queue failure and receive/control races. Hardware/TLS/API mocked.");
    return 0;
}
