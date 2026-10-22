#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include "hisu_ui_face.h"

typedef enum {
    HISU_UI_IDLE,
    HISU_UI_LISTENING,
    HISU_UI_THINKING,
    HISU_UI_SPEAKING,
    HISU_UI_ERROR,
    HISU_UI_MUSIC,
    HISU_UI_MUSIC_LOADING,
    HISU_UI_REMINDER,
    HISU_UI_SLEEP,
} hisu_ui_status_t;

esp_err_t hisu_ui_start(void);
typedef bool (*hisu_ui_voice_control_fn)(bool end_session);
/* Register before UI startup; callback must not block on network operations. */
void hisu_ui_set_voice_control(hisu_ui_voice_control_fn control);
void hisu_ui_set_voice_buttons(bool (*wake)(void), bool (*boot)(void));
void hisu_ui_set_playback_control(int (*mode)(bool cycle));
// Copies UTF-8 text (up to 255 bytes); returns an error on invalid input or full queue.
// Safe for worker tasks, not ISR callbacks. Caller never accesses LVGL.
esp_err_t hisu_ui_post_status(hisu_ui_status_t status, const char *subtitle);
/* Ticket-bound lyrics update the existing caption without rebuilding animation. */
esp_err_t hisu_ui_post_music(hisu_ui_status_t status, uint32_t ticket, const char *text);
esp_err_t hisu_ui_post_music_lyric(uint32_t ticket, const char *text, const char *next);
// Uses the UI task's existing flashlight state; safe for worker tasks.
esp_err_t hisu_ui_post_flashlight(bool enabled);
/* Ephemeral expression for the current reply; never persisted to flash. */
esp_err_t hisu_ui_post_expression(hisu_face_t face);
/* Lock-free audio meter; no LVGL calls or status-queue traffic from the audio worker. */
void hisu_ui_music_level(unsigned level);
