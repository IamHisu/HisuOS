#include <assert.h>
#include <stdio.h>
#include "hisu_ui_timing.h"

int main(void)
{
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
