#include "hisu_voice_music.h"
#include <string.h>
#include <stdio.h>

bool hisu_music_valid_id(const char *id)
{
    if (!id || !*id || strlen(id) > 40) return false;
    for (const unsigned char *p = (const unsigned char *)id; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9'))) return false;
    return true;
}

bool hisu_music_valid_url(const char *url)
{
    if (!url || strncmp(url,"https://",8) || strlen(url) >= 4096) return false;
    const char *host = url + 8, *end = strchr(host,'/');
    if (!end || end == host) return false;
    for (const char *p = host; *p; ++p)
        if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127 || *p == '\\' || *p == '#') return false;
    static const char *const suffixes[] = {".zmdcdn.me", ".zmdcdn.com", ".zingmp3.vn"};
    size_t host_len = (size_t)(end - host);
    for (unsigned i = 0; i < sizeof(suffixes)/sizeof(suffixes[0]); ++i) {
        size_t n = strlen(suffixes[i]);
        if (host_len > n && !memcmp(end - n,suffixes[i],n)) {
            for (const char *p = host; p < end; ++p)
                if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '-')) return false;
            return true;
        }
    }
    return false;
}

bool hisu_music_encode_query(const char *text, char *out, size_t capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    if (!text || !*text || strlen(text) > 180 || !out || !capacity) return false;
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || strchr("-._~",*p);
        size_t need = plain ? 1 : 3;
        if (used + need >= capacity) return false;
        if (plain) out[used++] = (char)*p;
        else { out[used++] = '%'; out[used++] = hex[*p >> 4]; out[used++] = hex[*p & 15]; }
    }
    out[used] = 0;
    return true;
}

static bool success(const cJSON *root)
{
    const cJSON *err = cJSON_GetObjectItemCaseSensitive(root,"err");
    return cJSON_IsNumber(err) && err->valuedouble == 0;
}

bool hisu_music_parse_search(const cJSON *root, cJSON *results)
{
    if (!success(root) || !cJSON_IsArray(results)) return false;
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root,"data");
    if (!cJSON_IsObject(data)) return false;
    const cJSON *songs = cJSON_GetObjectItemCaseSensitive(data,"items");
    if (!songs) songs = cJSON_GetObjectItemCaseSensitive(data,"songs");
    if (!songs) return true; /* A successful empty search is not a transport failure. */
    if (!cJSON_IsArray(songs)) return false;
    int count = cJSON_GetArraySize(songs);
    for (int i = 0; i < count && cJSON_GetArraySize(results) < 5; ++i) {
        const cJSON *song = cJSON_GetArrayItem(songs,i);
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(song,"encodeId");
        const cJSON *title = cJSON_GetObjectItemCaseSensitive(song,"title");
        const cJSON *artist = cJSON_GetObjectItemCaseSensitive(song,"artistsNames");
        if (!cJSON_IsString(id) || !hisu_music_valid_id(id->valuestring) || !cJSON_IsString(title) ||
            !cJSON_IsString(artist) || strlen(title->valuestring) >= 128 || strlen(artist->valuestring) >= 128) continue;
        cJSON *item = cJSON_CreateObject();
        if (!item) return false;
        bool ok = cJSON_AddStringToObject(item,"id",id->valuestring) &&
            cJSON_AddStringToObject(item,"title",title->valuestring) &&
            cJSON_AddStringToObject(item,"artist",artist->valuestring);
        if (!ok || !cJSON_AddItemToArray(results,item)) { cJSON_Delete(item); return false; }
    }
    return true;
}

bool hisu_music_parse_track(const cJSON *info, const cJSON *stream, hisu_music_track_t *track)
{
    if (!track || !success(info) || !success(stream)) return false;
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(info,"data");
    const cJSON *title = cJSON_GetObjectItemCaseSensitive(data,"title");
    const cJSON *artist = cJSON_GetObjectItemCaseSensitive(data,"artistsNames");
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(stream,"data"),"128");
    if (!cJSON_IsString(title) || !cJSON_IsString(artist) || !cJSON_IsString(url) ||
        strlen(title->valuestring) >= sizeof(track->title) || strlen(artist->valuestring) >= sizeof(track->artist) ||
        !hisu_music_valid_url(url->valuestring)) return false;
    memcpy(track->title,title->valuestring,strlen(title->valuestring)+1);
    memcpy(track->artist,artist->valuestring,strlen(artist->valuestring)+1);
    memcpy(track->url,url->valuestring,strlen(url->valuestring)+1);
    return true;
}
