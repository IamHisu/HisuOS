#pragma once

#include "friend_media.h"

esp_err_t friend_media_store_load(friend_media_kind_t *kind, uint8_t **data, size_t *size);
esp_err_t friend_media_store_load_image(friend_media_kind_t *kind, uint8_t **data, size_t *size);
esp_err_t friend_media_store_get_image_visible(bool *visible);
esp_err_t friend_media_store_set_image_visible(bool visible);
esp_err_t friend_media_store_prepare(friend_media_kind_t kind, const uint8_t *data, size_t size);
esp_err_t friend_media_store_commit(void);
void friend_media_store_abort(void);
