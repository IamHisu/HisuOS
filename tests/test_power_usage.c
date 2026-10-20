#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "settings_mock.h"
#define s_lock s_settings_lock
#include "../components/hisu_settings/src/hisu_settings.c"
#undef s_lock

static int mock_power_raw = 2252;
static bool mock_external;
static int mock_loops;
static jmp_buf task_exit;
static esp_err_t hisu_board_power_init(void) { return ESP_OK; }
static esp_err_t hisu_board_power_read(int *raw, bool *external) {
    *raw=mock_power_raw; *external=mock_external; return ESP_OK;
}
static void vTaskDelay(unsigned ticks) {
    mock_now+=(int64_t)ticks*1000;
    if (--mock_loops==0) longjmp(task_exit,1);
}
#include "../components/hisu_power/src/hisu_power.c"

static void reset_power(void) {
    s_lock=(void *)1; s_status=(hisu_power_status_t){.valid=true,.percent=50};
    s_usage=(hisu_power_usage_record_t){.version=2,.flags=USAGE_KNOWN};
    s_screen_known=true; s_screen_on=true;
    s_last_usage_us=mock_now=0; s_settle_until_battery_us=60000000;
    mock_power_usage_size=mock_power_writes=mock_power_write_failures=0;
    mock_external=false; mock_power_raw=2252;
}
static void run_task(int loops) {
    mock_loops=loops;
    if (!setjmp(task_exit)) power_task(NULL);
}
static void reboot_power(void) {
    s_lock=NULL; s_status=(hisu_power_status_t){0};
    s_usage=(hisu_power_usage_record_t){0};
    s_screen_known=false; mock_now=0;
    assert(hisu_power_start()==ESP_OK);
}

static void battery_cycle_tests(void) {
    reset_power();
    hisu_power_status_t battery={.valid=true,.percent=70};
    hisu_power_status_t charging={.valid=true,.external_power=true,.percent=70};
    hisu_power_status_t view;
    mock_now=120000000; account_usage();
    assert(hisu_settings_save_power_usage(&s_usage)==ESP_OK);
    mock_now=145000000; account_usage(); /* Unsaved tail is allowed to be lost. */
    reboot_power(); /* Time spent powered off is not part of the battery runtime. */
    update_power(battery);
    assert(s_usage.battery_us==120000000);
    mock_now=60000000; hisu_power_get(&view);
    assert(view.battery_seconds==180 && !view.previous_valid);
    assert(hisu_settings_save_power_usage(&s_usage)==ESP_OK);
    reboot_power();
    update_power(battery);
    mock_now=30000000; update_power(charging);
    hisu_power_get(&view);
    assert(view.battery_seconds==210 && view.previous_battery_seconds==210);
    mock_now=90000000; hisu_power_get(&view);
    assert(view.battery_seconds==210); /* Charging does not extend the discharge. */
    assert(hisu_settings_save_power_usage(&s_usage)==ESP_OK);
    reboot_power(); update_power(charging);
    assert(s_usage.battery_us==210000000 && s_usage.previous_battery_us==210000000);
    update_power(battery); /* Only the next unplug starts a fresh discharge. */
    hisu_power_get(&view);
    assert(view.battery_seconds==0 && view.previous_battery_seconds==210);
    mock_now=45000000; hisu_power_get(&view);
    assert(view.battery_seconds==45 && view.previous_battery_seconds==210);
}

static void battery_saver_tests(void) {
    reset_power();
    hisu_power_status_t power={.valid=true,.percent=11}, view;
    update_power(power); hisu_power_get(&view); assert(!view.power_saving);
    power.percent=10; update_power(power); hisu_power_get(&view); assert(view.power_saving);
    power.percent=14; update_power(power); hisu_power_get(&view); assert(view.power_saving);
    power.valid=false; update_power(power); hisu_power_get(&view); assert(view.power_saving);
    power.valid=true; power.percent=15; update_power(power); hisu_power_get(&view); assert(!view.power_saving);
    power.percent=0; update_power(power); hisu_power_get(&view); assert(view.power_saving);
    power.external_power=true; update_power(power); hisu_power_get(&view); assert(!view.power_saving);
    power.external_power=false; update_power(power); hisu_power_get(&view); assert(view.power_saving);
    assert(!mock_power_writes); /* Runtime policy never overwrites saved settings. */
    for (int percent=0;percent<=100;++percent) {
        assert(hisu_power_saving_next(0,1,0,percent)==(percent<=10));
        assert(hisu_power_saving_next(1,1,0,percent)==(percent<15));
        assert(!hisu_power_saving_next(1,1,1,percent));
    }
    assert(hisu_power_saving_next(1,0,0,0));
    assert(!hisu_power_saving_next(0,0,0,0));
    assert(hisu_power_saving_next(1,1,0,-1));
    assert(!hisu_power_saving_next(0,1,0,101));
}

int main(void) {
    battery_saver_tests();
    assert(hisu_settings_init()==ESP_OK);
    hisu_power_usage_record_t legacy={.version=1,.flags=1,.last_percent=20,
        .battery_us=100000000,.screen_on_us=80000000,.screen_sleep_us=20000000};
    mock_power_usage_size=offsetof(hisu_power_usage_record_t,total_active_us);
    memcpy(mock_power_usage,&legacy,mock_power_usage_size);
    hisu_power_usage_record_t loaded;
    assert(hisu_settings_load_power_usage(&loaded)==ESP_OK && loaded.version==2);
    assert(loaded.battery_us==100000000 && loaded.total_active_us==100000000);
    assert(hisu_settings_save_power_usage(&loaded)==ESP_OK);
    assert(hisu_settings_load_power_usage(&loaded)==ESP_OK && loaded.version==2);
    loaded.screen_on_us=loaded.battery_us+1;
    memcpy(mock_power_usage,&loaded,sizeof(loaded));
    assert(hisu_settings_load_power_usage(&loaded)==ESP_ERR_INVALID_SIZE);
    assert(loaded.battery_us==0 && loaded.total_active_us==0);

    reset_power();
    mock_now=10000000; account_usage();
    hisu_power_set_screen_on(false);
    mock_now=15000000; account_usage();
    assert(s_usage.battery_us==15000000 && s_usage.screen_on_us==10000000 &&
        s_usage.screen_sleep_us==5000000 && s_usage.total_active_us==15000000);
    assert(hisu_settings_save_power_usage(&s_usage)==ESP_OK);
    /* Sudden power loss: only the checkpoint, not the extra unsaved 3 seconds, survives. */
    mock_now=18000000; account_usage();
    s_lock=NULL; s_usage=(hisu_power_usage_record_t){0}; s_status=(hisu_power_status_t){0};
    s_screen_known=false; mock_now=0;
    assert(hisu_power_start()==ESP_OK);
    assert(s_usage.battery_us==15000000 && s_usage.total_active_us==15000000);
    hisu_power_status_t external={.valid=true,.external_power=true,.percent=30};
    update_power(external);
    assert((s_usage.flags&USAGE_PREVIOUS) && s_usage.previous_battery_us==15000000);
    mock_now=5000000;
    hisu_power_status_t view;
    hisu_power_get(&view);
    assert(view.total_active_seconds==20 && view.battery_seconds==15 &&
        view.previous_valid && view.previous_battery_seconds==15);
    hisu_power_status_t battery={.valid=true,.percent=50};
    update_power(battery);
    assert(s_usage.battery_us==0 && s_usage.screen_on_us==0 &&
        s_usage.previous_battery_us==15000000 && s_usage.total_active_us==20000000);
    mock_now=8000000; hisu_power_get(&view);
    assert(view.battery_seconds==3 && view.previous_battery_seconds==15 && view.total_active_seconds==23);
    assert(hisu_settings_save_power_usage(&s_usage)==ESP_OK);
    assert(hisu_settings_load_power_usage(&loaded)==ESP_OK && loaded.previous_battery_us==15000000);

    reset_power();
    assert(!checkpoint_due(&battery,59999999,0));
    assert(checkpoint_due(&battery,60000000,0));
    battery.percent=5;
    assert(!checkpoint_due(&battery,120000000,0));
    battery.percent=0;
    assert(!checkpoint_due(&battery,120000000,0));
    assert(checkpoint_due(&external,1000000,0)); /* Charging can preserve a finished run. */
    run_task(61);
    assert(mock_power_writes==2); /* At 60 and 120 seconds, not every ADC sample. */
    reset_power(); mock_power_raw=1950;
    run_task(61);
    assert(mock_power_writes==0); /* No last-second writes on an empty battery. */
    reset_power(); mock_power_write_failures=1;
    run_task(91);
    assert(mock_power_writes==3 && mock_power_write_failures==0);
    assert(hisu_settings_load_power_usage(&loaded)==ESP_OK && loaded.total_active_us==180000000);
    battery_cycle_tests();
    puts("Power usage tests passed: v1 migration, battery runtime across reboots, charging/unplug history, minute writes, low-battery guard and save retry. ADC/NVS/RTOS mocked.");
    return 0;
}
