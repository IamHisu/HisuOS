#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#define FRIEND_MEDIA_WIDTH 240
#define FRIEND_MEDIA_HEIGHT 240
#define FRIEND_MEDIA_STILL_BYTES (FRIEND_MEDIA_WIDTH * FRIEND_MEDIA_HEIGHT * 2)
#define FRIEND_MEDIA_GIF_MAX_BYTES (1024 * 1024)

typedef enum
{
    FRIEND_MEDIA_STILL,
    FRIEND_MEDIA_GIF,
    FRIEND_MEDIA_FACE
} friend_media_kind_t;

esp_err_t friend_media_init(void);
esp_err_t friend_media_submit(friend_media_kind_t kind, uint8_t *data, size_t size);
void friend_media_process(void);
const char *friend_media_current(void);
bool friend_media_gif_available(void);
void friend_media_toggle(void);
// Bind before starting the UI task; invoked on that task when showing media.
void friend_media_set_mode_changed_callback(void (*callback)(void));
