#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include "schedule_mock.h"
#include "../components/hisu_schedule/src/hisu_schedule.c"
static const int64_t NOW=1792108800LL;
static void reboot(void) {
    free(s_data); free(s_snapshot); s_data=s_snapshot=NULL; s_lock=NULL;
    assert(hisu_schedule_init()==ESP_OK);
}
static void reset(void) { memset(mock_flash,0xff,sizeof(mock_flash)); mock_budget=-1; reboot(); }
static void during_write(void) {
    uint32_t id; assert(hisu_schedule_set(0,NOW+120,"Second",NOW,&id)==ESP_OK);
}
int main(void) {
    _set_error_mode(_OUT_TO_STDERR);
    reset(); uint32_t id, duplicate;
    assert(hisu_schedule_local_utc("2024-01-01T07:00:00",420)==1704067200LL);
    assert(hisu_schedule_local_utc("2024-02-29T07:00:00",420)>0);
    assert(!hisu_schedule_local_utc("2025-02-29T07:00:00",420));
    assert(!hisu_schedule_local_utc("2026-10-16T24:00:00",420));
    assert(!hisu_schedule_local_utc("2026-10-16 12:00:00",420));
    assert(!hisu_schedule_local_utc("2026-10-16T12:00:00Z",420));
    assert(hisu_schedule_set(0,NOW+60,"Đi sửa xe",NOW,&id)==ESP_OK);
    assert(hisu_schedule_set(0,NOW+60,"Đi sửa xe",NOW,&duplicate)==ESP_OK && duplicate==id);
    assert(hisu_schedule_set(0,NOW,"Past",NOW,&duplicate)==ESP_ERR_INVALID_ARG);
    assert(hisu_schedule_set(0,NOW+1,"",NOW,&duplicate)==ESP_ERR_INVALID_ARG);
    assert(hisu_schedule_set(0,NOW+1,"\xc0\xaf",NOW,&duplicate)==ESP_ERR_INVALID_ARG);
    assert(hisu_schedule_flush()==ESP_OK); reboot();
    hisu_schedule_event_t event;
    assert(hisu_schedule_at(0,&event) && event.id==id && !strcmp(event.text,"Đi sửa xe"));
    assert(!hisu_schedule_due(NOW,&event));
    assert(hisu_schedule_due(NOW+60,&event) && event.id==id);
    assert(!hisu_schedule_due(NOW+60+86401,&event));
    assert(hisu_schedule_claim(id)==ESP_OK); reboot();
    assert(!hisu_schedule_due(NOW+61,&event));
    assert(hisu_schedule_set(id,NOW+120,"Đổi giờ",NOW,&duplicate)==ESP_OK);
    assert(hisu_schedule_due(NOW+121,&event));
    char *json=hisu_schedule_json(NOW,NOW+121); assert(json);
    cJSON *root=cJSON_Parse(json); free(json);
    assert(root && cJSON_GetArraySize(cJSON_GetObjectItem(root,"events"))==1); cJSON_Delete(root);
    assert(hisu_schedule_delete(id)==ESP_OK && hisu_schedule_flush()==ESP_OK); reboot();
    assert(!hisu_schedule_at(0,&event));
    for (int cut=0; cut<3; ++cut) {
        reset(); assert(hisu_schedule_set(0,NOW+60,"Original",NOW,&id)==ESP_OK);
        assert(hisu_schedule_flush()==ESP_OK);
        assert(hisu_schedule_set(id,NOW+120,"Replacement",NOW,&duplicate)==ESP_OK);
        mock_budget=cut; assert(hisu_schedule_flush()!=ESP_OK); mock_budget=-1; reboot();
        assert(hisu_schedule_at(0,&event) && !strcmp(event.text,"Original"));
    }
    reset(); assert(hisu_schedule_set(0,NOW+60,"Due",NOW,&id)==ESP_OK);
    assert(hisu_schedule_flush()==ESP_OK); mock_budget=0;
    assert(hisu_schedule_claim(id)!=ESP_OK && hisu_schedule_due(NOW+60,&event));
    mock_budget=-1;
    mock_hook=during_write; assert(hisu_schedule_flush()==ESP_OK && s_dirty);
    assert(hisu_schedule_flush()==ESP_OK); reboot(); assert(s_data->count==2);
    reset();
    for (unsigned i=0; i<HISU_SCHEDULE_COUNT; ++i) assert(hisu_schedule_set(0,NOW+1+i,"Plan",NOW,&id)==ESP_OK);
    assert(hisu_schedule_set(0,NOW+99,"Full",NOW,&id)==ESP_ERR_NO_MEM);
    assert(hisu_schedule_flush()==ESP_OK); reboot(); assert(s_data->count==32);
    puts("Schedule tests passed: strict time/UTF-8, CRUD, limits, range, due/catch-up, claim/reboot, torn writes and concurrent revision.");
    return 0;
}
