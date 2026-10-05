#include "board_buttons.h"
#include "board_config.h"

#include "esp_log.h"

#include "iot_button.h"
#include "button_gpio.h"

static const char *TAG = "BUTTON";

static button_handle_t s_btn_boot = NULL;
static button_handle_t s_btn_vol_up = NULL;
static button_handle_t s_btn_vol_down = NULL;

static board_button_callback_t s_boot_long_press_callback = NULL;
static board_button_callback_t s_boot_click_callback = NULL;

static void button_single_click_cb(void *button_handle, void *usr_data)
{
    const char *name = (const char *)usr_data;

    ESP_LOGI(TAG, "%s SINGLE_CLICK", name);
    if (button_handle == s_btn_boot && s_boot_click_callback != NULL)
    {
        s_boot_click_callback();
    }
}

static void button_long_press_cb(void *button_handle, void *usr_data)
{
    const char *name = (const char *)usr_data;

    ESP_LOGI(TAG, "%s LONG_PRESS", name);
}

static void boot_setup_long_press_cb(void *button_handle, void *usr_data)
{
    ESP_LOGI(TAG, "BOOT held 5 seconds");

    if (s_boot_long_press_callback != NULL)
    {
        s_boot_long_press_callback();
    }
}

static esp_err_t create_button(int gpio_num, const char *name, button_handle_t *handle)
{
    const button_config_t button_config = {
        .long_press_time = 1500,
        .short_press_time = 50,
    };

    const button_gpio_config_t gpio_config = {
        .gpio_num = gpio_num,
        .active_level = 0,
        .enable_power_save = false,
    };

    esp_err_t ret = iot_button_new_gpio_device(&button_config, &gpio_config, handle);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "%s create failed: %s", name, esp_err_to_name(ret));
        return ret;
    }

    ret = iot_button_register_cb(*handle, BUTTON_SINGLE_CLICK, NULL, button_single_click_cb, (void *)name);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "%s SINGLE_CLICK callback failed: %s", name, esp_err_to_name(ret));
        return ret;
    }

    ret = iot_button_register_cb(*handle, BUTTON_LONG_PRESS_START, NULL, button_long_press_cb, (void *)name);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "%s LONG_PRESS callback failed: %s", name, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "%s GPIO%d ready", name, gpio_num);

    return ESP_OK;
}

esp_err_t board_buttons_init(void)
{
    ESP_LOGI(TAG, "Initializing buttons with iot_button");

    ESP_ERROR_CHECK(create_button(BTN_BOOT, "BOOT", &s_btn_boot));
    ESP_ERROR_CHECK(create_button(BTN_VOL_UP, "VOL_UP", &s_btn_vol_up));
    ESP_ERROR_CHECK(create_button(BTN_VOL_DOWN, "VOL_DOWN", &s_btn_vol_down));

    button_event_args_t boot_setup_args = {
        .long_press.press_time = 5000,
    };

    esp_err_t ret = iot_button_register_cb(s_btn_boot, BUTTON_LONG_PRESS_START, &boot_setup_args, boot_setup_long_press_cb, NULL);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "BOOT 5-second callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "BOOT manual setup hold: 5000 ms");
    ESP_LOGI(TAG, "Buttons initialized");

    return ESP_OK;
}

void board_buttons_set_boot_long_press_callback(board_button_callback_t callback)
{
    s_boot_long_press_callback = callback;
}

void board_buttons_set_boot_click_callback(board_button_callback_t callback)
{
    s_boot_click_callback = callback;
}
