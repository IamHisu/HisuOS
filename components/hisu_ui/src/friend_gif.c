#include "friend_gif.h"

#include <limits.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "libs/gif/AnimatedGIF/src/AnimatedGIF.h"
#include "misc/cache/instance/lv_image_cache.h"

static const char *TAG = "friend_gif";

static uint16_t rgb565(const uint8_t *rgb)
{
    return ((uint16_t)(rgb[0] & 0xf8) << 8) |
           ((uint16_t)(rgb[1] & 0xfc) << 3) |
           (rgb[2] >> 3);
}

typedef struct
{
    GIFIMAGE gif;
    lv_image_dsc_t image;
    lv_timer_t *timer;
    lv_obj_t *object;
    uint8_t *framebuffer;
    uint8_t *saved_canvas;
    bool has_frame;
    bool frame_started;
    bool new_loop;
    bool draw_failed;
    uint32_t transparent_pixels;
    int32_t loops_remaining;
} friend_gif_t;

static void fill_background(friend_gif_t *player, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    GIFIMAGE *gif = &player->gif;
    uint32_t width = gif->iCanvasWidth;
    uint32_t height = gif->iCanvasHeight;
    if (x >= width || y >= height) return;
    if (w > width - x) w = width - x;
    if (h > height - y) h = height - y;

    for (uint32_t row = y; row < y + h; row++)
    {
        uint16_t *pixels = (uint16_t *)player->framebuffer + (size_t)row * width + x;
        memset(pixels, 0, w * sizeof(*pixels));
    }
}

static void apply_previous_disposal(friend_gif_t *player)
{
    if (!player->has_frame) return;
    GIFIMAGE *gif = &player->gif;
    uint8_t method = (gif->ucGIFBits >> 2) & 7;
    if (method == 2)
    {
        fill_background(player, gif->iX, gif->iY, gif->iWidth, gif->iHeight);
    }
    else if (method == 3 && player->saved_canvas != NULL)
    {
        memcpy(player->framebuffer, player->saved_canvas,
               (size_t)gif->iCanvasWidth * gif->iCanvasHeight * 2);
    }
}

static void draw_gif_line(GIFDRAW *draw)
{
    friend_gif_t *player = draw->pUser;
    if (player->draw_failed) return;

    size_t canvas_bytes = (size_t)player->gif.iCanvasWidth * player->gif.iCanvasHeight * 2;
    if (!player->frame_started)
    {
        player->frame_started = true;
        if (player->new_loop)
        {
            fill_background(player, 0, 0, player->gif.iCanvasWidth, player->gif.iCanvasHeight);
        }
        if (draw->ucDisposalMethod == 3)
        {
            if (player->saved_canvas == NULL)
                player->saved_canvas = heap_caps_malloc(canvas_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (player->saved_canvas == NULL)
            {
                player->draw_failed = true;
                return;
            }
            memcpy(player->saved_canvas, player->framebuffer, canvas_bytes);
        }
    }

    int32_t y = draw->iY + draw->y;
    if (y < 0 || y >= player->gif.iCanvasHeight) return;
    const uint8_t *palette = draw->pPalette24;
    for (int32_t col = 0; col < draw->iWidth; col++)
    {
        int32_t x = draw->iX + col;
        uint8_t index = draw->pPixels[col];
        if (x < 0 || x >= player->gif.iCanvasWidth) continue;
        uint16_t *pixel = (uint16_t *)player->framebuffer + (size_t)y * player->gif.iCanvasWidth + x;
        if (draw->ucHasTransparency && index == draw->ucTransparent)
        {
            player->transparent_pixels++;
            continue;
        }
        else
        {
            *pixel = rgb565(palette + index * 3);
        }
    }
}

static void next_frame(lv_timer_t *timer)
{
    friend_gif_t *player = lv_timer_get_user_data(timer);
    GIFIMAGE *gif = &player->gif;

    player->new_loop = !player->has_frame || gif->GIFFile.iPos >= gif->GIFFile.iSize - 1;
    if (!player->new_loop) apply_previous_disposal(player);
    player->frame_started = false;
    player->draw_failed = false;
    player->transparent_pixels = 0;

    int delay_ms = 0;
    int has_next = GIF_playFrame(gif, &delay_ms, player);
    if (player->draw_failed || GIF_getLastError(gif) != GIF_SUCCESS)
    {
        ESP_LOGE(TAG, "GIF frame decode failed or disposal buffer unavailable");
        lv_timer_pause(timer);
        return;
    }

    player->has_frame = true;
    lv_image_cache_drop(&player->image);
    lv_obj_invalidate(player->object);

    if (has_next == 0 && player->loops_remaining > 0)
    {
        player->loops_remaining--;
        if (player->loops_remaining == 0)
        {
            lv_timer_pause(timer);
            return;
        }
    }
    lv_timer_set_period(timer, delay_ms > 0 ? delay_ms : 10);
}

static void player_deleted(lv_event_t *event)
{
    friend_gif_t *player = lv_event_get_user_data(event);
    lv_timer_delete(player->timer);
    lv_image_cache_drop(&player->image);
    GIF_close(&player->gif);
    heap_caps_free(player->saved_canvas);
    lv_free(player->framebuffer);
    lv_free(player);
}

lv_obj_t *friend_gif_create(lv_obj_t *parent, uint8_t *data, size_t size)
{
    if (size > INT_MAX) return NULL;

    friend_gif_t *player = lv_malloc(sizeof(*player));
    if (player == NULL) return NULL;
    memset(player, 0, sizeof(*player));

    GIF_begin(&player->gif, GIF_PALETTE_RGB8888);
    if (!GIF_openRAM(&player->gif, data, (int)size, draw_gif_line)) goto fail;

    uint32_t width = GIF_getCanvasWidth(&player->gif);
    uint32_t height = GIF_getCanvasHeight(&player->gif);
    if (width == 0 || height == 0 || width > 240 || height > 240) goto fail_close;

    size_t pixel_count = (size_t)width * height;
    player->framebuffer = lv_malloc(pixel_count * 2);
    if (player->framebuffer == NULL) goto fail_close;
    memset(player->framebuffer, 0, pixel_count * 2);
    player->gif.ucDrawType = GIF_DRAW_RAW;

    player->image = (lv_image_dsc_t){
        .header.magic = LV_IMAGE_HEADER_MAGIC,
        .header.flags = LV_IMAGE_FLAGS_MODIFIABLE,
        .header.cf = LV_COLOR_FORMAT_RGB565,
        .header.w = width,
        .header.h = height,
        .header.stride = width * 2,
        .data_size = pixel_count * 2,
        .data = player->framebuffer,
    };
    player->loops_remaining = GIF_getLoopCount(&player->gif);

    player->object = lv_image_create(parent);
    if (player->object == NULL) goto fail_buffer;
    lv_image_set_src(player->object, &player->image);
    player->timer = lv_timer_create(next_frame, 10, player);
    if (player->timer == NULL) goto fail_object;
    lv_obj_add_event_cb(player->object, player_deleted, LV_EVENT_DELETE, player);
    next_frame(player->timer);
    if (!player->has_frame)
    {
        lv_obj_delete(player->object);
        return NULL;
    }
    ESP_LOGI(TAG, "GIF ready: %ux%u, opaque RGB565, transparent=%u, first corner=0x%04x",
             (unsigned int)width, (unsigned int)height, (unsigned int)player->transparent_pixels,
             ((uint16_t *)player->framebuffer)[0]);
    return player->object;

fail_object:
    lv_image_cache_drop(&player->image);
    lv_obj_delete(player->object);
fail_buffer:
    lv_free(player->framebuffer);
fail_close:
    GIF_close(&player->gif);
fail:
    lv_free(player);
    return NULL;
}
