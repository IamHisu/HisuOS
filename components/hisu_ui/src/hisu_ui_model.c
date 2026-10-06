#include "hisu_ui_model.h"

int hisu_ui_adjust(int setting, int value, int direction)
{
    if (direction != -1 && direction != 1) return value;
    if (setting == UI_SPEAKER || setting == UI_THEME) return direction > 0;
    if (setting == UI_TIMEOUT) {
        const int presets[] = {0, 30, 60, 120, 300, 600};
        if (direction > 0) {
            for (int i = 0; i < 6; ++i) if (presets[i] > value) return presets[i];
            return 600;
        }
        for (int i = 5; i >= 0; --i) if (presets[i] < value) return presets[i];
        return 0;
    }
    if (setting != UI_BRIGHTNESS && setting != UI_VOLUME) return value;
    int min = setting == UI_BRIGHTNESS ? 10 : 0;
    value += direction * 10;
    return value < min ? min : value > 100 ? 100 : value;
}

void hisu_ui_model_key(hisu_ui_model_t *m, int key)
{
    if (key < UI_KEY_BOOT || key > UI_KEY_BACK) return;
    if (key == UI_KEY_BACK) {
        m->page = m->page == UI_MENU ? UI_IDLE : UI_MENU;
        return;
    }
    switch (m->page) {
    case UI_IDLE:
        if (key == UI_KEY_BOOT) m->page = UI_MENU;
        break;
    case UI_MENU:
        if (key == UI_KEY_UP) m->selected = (m->selected + UI_ITEM_COUNT - 1) % UI_ITEM_COUNT;
        if (key == UI_KEY_DOWN) m->selected = (m->selected + 1) % UI_ITEM_COUNT;
        if (m->selected < m->first_visible) m->first_visible = m->selected;
        if (m->selected >= m->first_visible + 5) m->first_visible = m->selected - 4;
        if (key == UI_KEY_BOOT) {
            if (m->selected <= UI_THEME) m->page = UI_EDIT;
            else if (m->selected == UI_WIFI) m->page = UI_WIFI_PAGE;
            else if (m->selected == UI_CALENDAR) m->page = UI_CALENDAR_PAGE;
            else if (m->selected == UI_SETUP) m->page = UI_SETUP_PAGE;
            else if (m->selected == UI_SCREEN_OFF || m->selected == UI_CLOSE) m->page = UI_IDLE;
            else m->page = UI_INFO;
        }
        break;
    case UI_EDIT:
        if (key == UI_KEY_BOOT) m->page = UI_MENU;
        else if (m->selected == UI_SPEAKER || m->selected == UI_THEME) {
            if (key == UI_KEY_DOWN) m->value = !m->value;
            else if (key == UI_KEY_UP) m->page = UI_MENU;
        }
        else m->value = hisu_ui_adjust(m->selected, m->value, key == UI_KEY_UP ? 1 : -1);
        break;
    case UI_SETUP_PAGE:
        if (key == UI_KEY_UP) m->page = UI_MENU;
        else if (key == UI_KEY_DOWN) m->preview = (m->preview + 1) % UI_SETUP_VIEW_COUNT;
        else if (key == UI_KEY_BOOT) m->preview = UI_SETUP_QR;
        break;
    case UI_INFO:
        if (key == UI_KEY_DOWN) m->page = UI_MENU; /* physical left button: Vol- */
        break;
    case UI_PREVIEW:
        if (key == UI_KEY_BOOT || key == UI_KEY_DOWN) {
            if (++m->preview == 3) m->page = UI_MENU;
        } else if (key == UI_KEY_UP && m->preview > 0) --m->preview;
        break;
    }
}

int hisu_ui_overlay_dark_ink(const unsigned char *pixels, unsigned width, unsigned height,
                             unsigned stride, int x, int y, int w, int h, int previous)
{
    int fallback = previous > 0;
    if (!pixels || !width || !height || width > 512 || height > 512 ||
        stride < width * 2 || w <= 0 || h <= 0) return fallback;
    unsigned sum = 0, count = 0;
    for (int py = y; py < y + h; py += 2) {
        if (py < 0 || (unsigned)py >= height) continue;
        for (int px = x; px < x + w; px += 2) {
            if (px < 0 || (unsigned)px >= width) continue;
            const unsigned char *p = pixels + (unsigned)py * stride + (unsigned)px * 2;
            unsigned rgb = p[0] | ((unsigned)p[1] << 8);
            unsigned r = ((rgb >> 11) & 31) * 255 / 31;
            unsigned g = ((rgb >> 5) & 63) * 255 / 63;
            unsigned b = (rgb & 31) * 255 / 31;
            sum += (77 * r + 150 * g + 29 * b) >> 8;
            ++count;
        }
    }
    if (!count) return fallback;
    unsigned light = sum / count;
    /* A dead band keeps moving GIF pixels from flipping the overlay each frame. */
    if (previous < 0) return light >= 128;
    return previous ? light >= 104 : light > 152;
}

int hisu_ui_valid_utf8(const char *text, unsigned max_bytes)
{
    if (!text) return 0;
    unsigned pos = 0;
    while (text[pos]) {
        if (pos >= max_bytes) return 0;
        unsigned char c = (unsigned char)text[pos++];
        if (c < 0x80) continue;
        int remaining;
        unsigned code;
        unsigned minimum;
        if (c >= 0xc2 && c <= 0xdf) {remaining = 1; code = c & 0x1f; minimum = 0x80;}
        else if (c >= 0xe0 && c <= 0xef) {remaining = 2; code = c & 0x0f; minimum = 0x800;}
        else if (c >= 0xf0 && c <= 0xf4) {remaining = 3; code = c & 7; minimum = 0x10000;}
        else return 0;
        while (remaining--) {
            if (pos >= max_bytes) return 0;
            unsigned char next = (unsigned char)text[pos++];
            if ((next & 0xc0) != 0x80) return 0;
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return 0;
    }
    return 1;
}
