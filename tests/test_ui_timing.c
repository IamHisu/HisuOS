#include <assert.h>
#include <stdio.h>
#include "hisu_ui_timing.h"

int main(void)
{
    bool latched=false;
    assert(hisu_ui_audio_release(&latched,false,true)==0);
    assert(hisu_ui_audio_release(&latched,true,true)==-1 && latched);
    assert(hisu_ui_audio_release(&latched,true,true)==-1);
    assert(hisu_ui_audio_release(&latched,false,true)==1 && !latched);
    assert(hisu_ui_audio_release(&latched,false,true)==0);
    assert(hisu_ui_audio_release(&latched,true,true)==-1);
    assert(hisu_ui_audio_release(&latched,false,false)==-1 && !latched);
    hisu_ui_boot_click_t click={0};
    assert(hisu_ui_boot_click(&click,true,false,true,false,1000)==HISU_BOOT_CLICK_NONE);
    assert(hisu_ui_boot_click(&click,false,false,true,false,1350)==HISU_BOOT_CLICK_NONE);
    assert(hisu_ui_boot_click(&click,true,false,true,false,1350)==HISU_BOOT_CLICK_DOUBLE);
    assert(hisu_ui_boot_click(&click,false,false,true,false,1800)==HISU_BOOT_CLICK_NONE);
    assert(hisu_ui_boot_click(&click,true,false,true,false,2000)==HISU_BOOT_CLICK_NONE);
    assert(hisu_ui_boot_click(&click,false,false,true,false,2451)==HISU_BOOT_CLICK_SINGLE);
    (void)hisu_ui_boot_click(&click,true,false,true,true,3000);
    assert(hisu_ui_boot_click(&click,false,false,true,false,3400)==HISU_BOOT_CLICK_NONE);
    (void)hisu_ui_boot_click(&click,true,false,true,true,4000);
    assert(hisu_ui_boot_click(&click,true,false,true,false,4200)==HISU_BOOT_CLICK_DOUBLE);
    (void)hisu_ui_boot_click(&click,true,false,true,false,5000);
    assert(hisu_ui_boot_click(&click,false,true,true,false,5400)==HISU_BOOT_CLICK_NONE);
    assert(hisu_ui_boot_click(&click,false,false,false,false,5500)==HISU_BOOT_CLICK_NONE);
    assert(hisu_ui_boot_click(&click,false,false,true,false,5800)==HISU_BOOT_CLICK_NONE);
    (void)hisu_ui_boot_click(&click,true,false,true,false,6000);
    assert(hisu_ui_boot_click(&click,false,false,false,false,6200)==HISU_BOOT_CLICK_NONE);
    assert(!click.pending); /* New voice state/menu/key cancels delayed menu entry. */
    click=(hisu_ui_boot_click_t){0};
    assert(hisu_ui_boot_click(&click,true,false,true,false,7000)==HISU_BOOT_CLICK_NONE);
    assert(hisu_ui_boot_click(&click,false,true,true,false,7400)==HISU_BOOT_CLICK_NONE);
    assert(hisu_ui_boot_click(&click,false,true,true,false,7500)==HISU_BOOT_CLICK_NONE);
    assert(hisu_ui_boot_click(&click,true,false,true,false,7600)==HISU_BOOT_CLICK_DOUBLE);
    assert(!click.pending && !click.second_pressed); /* Second press began on time; release can be later. */
    assert(hisu_ui_boot_click(&click,false,false,true,false,8000)==HISU_BOOT_CLICK_NONE);
    (void)hisu_ui_boot_click(&click,true,false,true,false,8100);
    (void)hisu_ui_boot_click(&click,false,true,true,false,8400);
    assert(hisu_ui_boot_click(&click,true,false,true,false,8600)==HISU_BOOT_CLICK_DOUBLE);
    (void)hisu_ui_boot_click(&click,true,false,true,false,9000);
    (void)hisu_ui_boot_click(&click,false,true,true,false,9300);
    assert(hisu_ui_boot_click(&click,true,false,true,false,10100)==HISU_BOOT_CLICK_NONE);
    assert(!click.pending); /* Holding the second press is not a double click. */
    (void)hisu_ui_boot_click(&click,true,false,true,false,11000);
    (void)hisu_ui_boot_click(&click,false,true,true,false,11300);
    (void)hisu_ui_boot_click(&click,false,true,false,false,11350);
    assert(!click.pending && !click.second_pressed);
    for (int brightness = 0; brightness <= 100; ++brightness) {
        assert(hisu_ui_power_brightness(false, brightness) == brightness);
        assert(hisu_ui_power_brightness(true, brightness) == (brightness > 10 ? 10 : brightness));
    }
    const int timeouts[] = {0, 5, 15, 30, 60, 120, 300, 600};
    for (unsigned i = 0; i < sizeof(timeouts) / sizeof(timeouts[0]); ++i) {
        int timeout = timeouts[i];
        assert(hisu_ui_power_timeout(false, timeout) == timeout);
        assert(hisu_ui_power_timeout(true, timeout) == (timeout == 0 || timeout > 15 ? 15 : timeout));
    }
    assert(hisu_ui_wait_ms(0) == 1);
    for (uint32_t ms = 1; ms <= 10; ++ms) {
        assert(hisu_ui_wait_ms(ms) == ms);
    }
    assert(hisu_ui_wait_ms(16) == 10);
    assert(hisu_ui_wait_ms(40) == 10);
    assert(hisu_ui_wait_ms(100) == 10);
    assert(hisu_ui_wait_ms(UINT32_MAX) == 10);
    assert(!hisu_ui_error_due(true, 1000, 3999));
    assert(hisu_ui_error_due(true, 1000, 4000));
    assert(hisu_ui_error_due(true, 1000, 5000));
    assert(!hisu_ui_error_due(false, 1000, 5000));
    assert(!hisu_ui_error_due(true, 4500, 5000));
    assert(!hisu_ui_error_due(true, 1000, 999));
    assert(hisu_ui_error_due(true, INT64_C(5000000000), INT64_C(5000003000)));
    puts("UI timing tests passed");
    return 0;
}
