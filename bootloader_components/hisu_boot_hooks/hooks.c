#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"
#include "esp_rom_sys.h"

void bootloader_hooks_include(void) {}

/* GPIO20 is the Zhengchen backlight and USB D+. GPIO45 resets the ST7789.
 * Assert both around bootloader initialization so neither USB setup nor the
 * panel's retained GRAM can expose the previous frame. */
static void keep_backlight_dark(void)
{
    gpio_ll_set_level(&GPIO, 20, 0);
    gpio_ll_func_sel(&GPIO, 20, PIN_FUNC_GPIO);
    gpio_ll_matrix_out_default(&GPIO, 20);
    gpio_ll_output_enable(&GPIO, 20);
    /* Keep the shared USB D+ pad low until the app installs zero-duty PWM. */
    gpio_ll_hold_en(&GPIO, 20);
}

static void keep_panel_reset(void)
{
    gpio_ll_set_level(&GPIO, 45, 0);
    gpio_ll_func_sel(&GPIO, 45, PIN_FUNC_GPIO);
    gpio_ll_matrix_out_default(&GPIO, 45);
    gpio_ll_output_enable(&GPIO, 45);
}

void bootloader_before_init(void)
{
    keep_backlight_dark();
    /* Let the backlight discharge before clearing the retained LCD frame. */
    esp_rom_delay_us(20000);
    keep_panel_reset();
}

void bootloader_after_init(void)
{
    /* Bootloader initialization may touch the shared USB D+ pad. */
    keep_backlight_dark();
    keep_panel_reset();
}
