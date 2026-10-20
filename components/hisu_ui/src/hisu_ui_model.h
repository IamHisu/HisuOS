#pragma once

enum { HISU_UI_BATTERY_BARS = 4 };

enum {UI_IDLE, UI_MENU, UI_EDIT, UI_INFO, UI_PREVIEW, UI_WIFI_PAGE, UI_WIFI_PASSWORD, UI_CALENDAR_PAGE, UI_FLASHLIGHT_PAGE, UI_SETUP_PAGE};
enum {UI_KEY_BOOT = 1, UI_KEY_UP, UI_KEY_DOWN, UI_KEY_BACK};
enum {UI_SETUP_QR, UI_SETUP_INFO, UI_SETUP_RESET, UI_SETUP_VIEW_COUNT};
enum {UI_BRIGHTNESS, UI_VOLUME, UI_SPEAKER, UI_TIMEOUT, UI_THEME, UI_WIFI,
      UI_BATTERY, UI_CALENDAR, UI_SETUP, UI_SCREEN_OFF, UI_CLOSE, UI_ITEM_COUNT};

typedef struct {
    int page;
    int selected;
    int value;
    int preview;
    int first_visible;
} hisu_ui_model_t;

void hisu_ui_model_key(hisu_ui_model_t *model, int key);
int hisu_ui_adjust(int setting, int value, int direction);
int hisu_ui_battery_bars(int valid, int percent);
unsigned hisu_ui_battery_color(int bars, unsigned frame_color);
unsigned hisu_ui_battery_pulse(unsigned elapsed_ms);
int hisu_ui_battery_charge_bar(int valid, int percent);
int hisu_ui_valid_utf8(const char *text, unsigned max_bytes);
int hisu_ui_overlay_dark_ink(const unsigned char *pixels, unsigned width, unsigned height,
                             unsigned stride, int x, int y, int w, int h, int previous);
