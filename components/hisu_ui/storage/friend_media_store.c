#include "friend_media_store.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "nvs.h"

#define STORE_MAGIC 0x4d4f4331u
#define STORE_VERSION 1u
#define SLOT_SIZE 0x110000u
#define DATA_OFFSET 0x1000u

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t kind;
    uint32_t size;
    uint32_t data_hash;
    uint32_t header_hash;
} media_header_t;

static const char *TAG = "MEDIA_STORE";
static const esp_partition_t *partition;
static int active_slot = -1;
static uint32_t active_sequence;
static friend_media_kind_t active_kind = FRIEND_MEDIA_FACE;
static int pending_slot = -1;
static media_header_t pending_header;

static uint32_t hash_bytes(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; i++) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

static esp_err_t find_partition(void)
{
    if (partition == NULL)
        partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "media");
    if (partition == NULL || partition->size < 2 * SLOT_SIZE) return ESP_ERR_NOT_FOUND;
    return ESP_OK;
}

static bool header_valid(const media_header_t *header)
{
    if (header->magic != STORE_MAGIC || header->version != STORE_VERSION ||
        header->size > FRIEND_MEDIA_GIF_MAX_BYTES ||
        header->header_hash != hash_bytes(header, offsetof(media_header_t, header_hash))) return false;
    if (header->kind == FRIEND_MEDIA_FACE) return header->size == 0;
    if (header->kind == FRIEND_MEDIA_STILL) return header->size == FRIEND_MEDIA_STILL_BYTES;
    return header->kind == FRIEND_MEDIA_GIF && header->size >= 13;
}

static esp_err_t read_slot(int slot, const media_header_t *header, uint8_t **data)
{
    *data = NULL;
    if (header->size == 0)
        return header->data_hash == hash_bytes(NULL, 0) ? ESP_OK : ESP_ERR_INVALID_CRC;

    uint8_t *buffer = heap_caps_malloc(header->size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_partition_read(partition, slot * SLOT_SIZE + DATA_OFFSET, buffer, header->size);
    if (err == ESP_OK && hash_bytes(buffer, header->size) != header->data_hash)
        err = ESP_ERR_INVALID_CRC;
    if (err != ESP_OK) free(buffer);
    else *data = buffer;
    return err;
}

esp_err_t friend_media_store_load(friend_media_kind_t *kind, uint8_t **data, size_t *size)
{
    if (kind == NULL || data == NULL || size == NULL) return ESP_ERR_INVALID_ARG;
    *data = NULL;
    *size = 0;
    esp_err_t err = find_partition();
    if (err != ESP_OK) return err;

    media_header_t headers[2];
    bool valid[2];
    for (int slot = 0; slot < 2; slot++)
    {
        err = esp_partition_read(partition, slot * SLOT_SIZE, &headers[slot], sizeof(headers[slot]));
        valid[slot] = err == ESP_OK && header_valid(&headers[slot]);
    }
    int first = valid[0] ? 0 : 1;
    if (valid[0] && valid[1] && (int32_t)(headers[1].sequence - headers[0].sequence) > 0)
        first = 1;

    for (int attempt = 0; attempt < 2; attempt++)
    {
        int slot = attempt == 0 ? first : 1 - first;
        if (!valid[slot]) continue;
        err = read_slot(slot, &headers[slot], data);
        if (err == ESP_ERR_NO_MEM) return err;
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Saved media slot %d failed verification: %s", slot, esp_err_to_name(err));
            continue;
        }
        active_slot = slot;
        active_sequence = headers[slot].sequence;
        active_kind = (friend_media_kind_t)headers[slot].kind;
        *kind = (friend_media_kind_t)headers[slot].kind;
        *size = headers[slot].size;
        ESP_LOGI(TAG, "Restored %s from slot %d (%u bytes)",
                 *kind == FRIEND_MEDIA_GIF ? "GIF" : *kind == FRIEND_MEDIA_STILL ? "image" : "face",
                 slot, (unsigned int)*size);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t friend_media_store_load_image(friend_media_kind_t *kind, uint8_t **data, size_t *size)
{
    if (kind == NULL || data == NULL || size == NULL) return ESP_ERR_INVALID_ARG;
    *data = NULL;
    *size = 0;
    esp_err_t err = find_partition();
    if (err != ESP_OK) return err;

    media_header_t headers[2];
    bool valid[2];
    for (int slot = 0; slot < 2; slot++)
    {
        err = esp_partition_read(partition, slot * SLOT_SIZE, &headers[slot], sizeof(headers[slot]));
        valid[slot] = err == ESP_OK && header_valid(&headers[slot]) &&
                      headers[slot].kind != FRIEND_MEDIA_FACE;
    }
    int first = valid[0] ? 0 : 1;
    if (valid[0] && valid[1] && (int32_t)(headers[1].sequence - headers[0].sequence) > 0)
        first = 1;

    for (int attempt = 0; attempt < 2; attempt++)
    {
        int slot = attempt == 0 ? first : 1 - first;
        if (!valid[slot]) continue;
        err = read_slot(slot, &headers[slot], data);
        if (err == ESP_ERR_NO_MEM) return err;
        if (err != ESP_OK) continue;
        *kind = (friend_media_kind_t)headers[slot].kind;
        *size = headers[slot].size;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t friend_media_store_get_image_visible(bool *visible)
{
    if (visible == NULL) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("friend_media", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    uint8_t value;
    err = nvs_get_u8(handle, "show_image", &value);
    nvs_close(handle);
    if (err == ESP_OK) *visible = value != 0;
    return err;
}

esp_err_t friend_media_store_set_image_visible(bool visible)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("friend_media", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, "show_image", visible ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t friend_media_store_prepare(friend_media_kind_t kind, const uint8_t *data, size_t size)
{
    if (pending_slot >= 0) return ESP_ERR_INVALID_STATE;
    if ((kind == FRIEND_MEDIA_FACE && (data != NULL || size != 0)) ||
        (kind == FRIEND_MEDIA_STILL && (data == NULL || size != FRIEND_MEDIA_STILL_BYTES)) ||
        (kind == FRIEND_MEDIA_GIF && (data == NULL || size < 13 || size > FRIEND_MEDIA_GIF_MAX_BYTES)) ||
        (kind != FRIEND_MEDIA_FACE && kind != FRIEND_MEDIA_STILL && kind != FRIEND_MEDIA_GIF))
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = find_partition();
    if (err != ESP_OK) return err;

    int slot = active_slot >= 0 && active_kind == FRIEND_MEDIA_FACE
        ? active_slot : active_slot == 0 ? 1 : 0;
    size_t erase_size = DATA_OFFSET + ((size + DATA_OFFSET - 1) & ~(DATA_OFFSET - 1));
    err = esp_partition_erase_range(partition, slot * SLOT_SIZE, erase_size);
    if (err == ESP_OK && size != 0)
    {
        uint8_t *chunk = heap_caps_malloc(DATA_OFFSET, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (chunk == NULL) return ESP_ERR_NO_MEM;
        for (size_t offset = 0; offset < size && err == ESP_OK; offset += DATA_OFFSET)
        {
            size_t length = size - offset < DATA_OFFSET ? size - offset : DATA_OFFSET;
            memcpy(chunk, data + offset, length);
            err = esp_partition_write(partition, slot * SLOT_SIZE + DATA_OFFSET + offset, chunk, length);
        }
        free(chunk);
    }
    if (err != ESP_OK) return err;

    pending_header = (media_header_t){
        .magic = STORE_MAGIC,
        .version = STORE_VERSION,
        .sequence = active_sequence + 1,
        .kind = kind,
        .size = size,
        .data_hash = hash_bytes(data, size),
    };
    pending_header.header_hash = hash_bytes(&pending_header, offsetof(media_header_t, header_hash));
    pending_slot = slot;
    return ESP_OK;
}

esp_err_t friend_media_store_commit(void)
{
    if (pending_slot < 0) return ESP_ERR_INVALID_STATE;
    int slot = pending_slot;
    pending_slot = -1;
    esp_err_t err = esp_partition_write(partition, slot * SLOT_SIZE, &pending_header, sizeof(pending_header));
    if (err == ESP_OK)
    {
        active_slot = slot;
        active_sequence = pending_header.sequence;
        active_kind = (friend_media_kind_t)pending_header.kind;
    }
    return err;
}

void friend_media_store_abort(void)
{
    pending_slot = -1;
}
