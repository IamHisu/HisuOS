#include <assert.h>
#include <stdio.h>
#include "hisu_ui_timing.h"

int main(void)
{
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
