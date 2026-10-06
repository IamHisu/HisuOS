#include "hisu_ble.h"
#include "hisu_network.h"
#include "hisu_clock.h"
#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* e3a10000-7d4b-4ee0-b417-6d9f67485501; characteristics end in 0001..0007. */
#define HISU_UUID(n) BLE_UUID128_INIT(0x01,0x55,0x48,0x67,0x9f,0x6d,0x17,0xb4,0xe0,0x4e,0x4b,0x7d,n,0x00,0xa1,0xe3)
static const ble_uuid128_t s_uuid = HISU_UUID(0);
static const ble_uuid128_t s_chars[] = {HISU_UUID(1),HISU_UUID(2),HISU_UUID(3),HISU_UUID(4),HISU_UUID(5),HISU_UUID(6),HISU_UUID(7)};
/* Freeze a complete value across ATT Read Blob fragments (one BLE connection). */
static uint8_t s_ap_value[7+HISU_WIFI_MAX_APS*35];
static unsigned s_ap_length;
static SemaphoreHandle_t s_lock;
static hisu_ble_status_t s_status;
static bool s_requested;
static unsigned s_requested_minutes = 5;
static uint8_t s_addr_type;
static uint16_t s_connection = BLE_HS_CONN_HANDLE_NONE;
static char s_ssid[33], s_password[65];
static struct ble_npl_event s_control;
static struct ble_npl_callout s_timeout;
static int gap_event(struct ble_gap_event *event, void *arg);

static void clear_input(void) { memset(s_ssid, 0, sizeof(s_ssid)); memset(s_password, 0, sizeof(s_password)); }
void hisu_ble_get(hisu_ble_status_t *out)
{
    if (!out) return;
    *out = (hisu_ble_status_t){0};
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY); *out = s_status; xSemaphoreGive(s_lock);
}
static void report_error(int rc)
{
    xSemaphoreTake(s_lock, portMAX_DELAY); s_status.error = rc; xSemaphoreGive(s_lock);
}
static void advertise(void)
{
    hisu_ble_status_t status; hisu_ble_get(&status);
    if (!status.ready || !status.enabled || s_connection != BLE_HS_CONN_HANDLE_NONE || ble_gap_adv_active()) return;
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&s_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    struct ble_hs_adv_fields response = {0};
    response.name = (uint8_t *)s_status.name;
    response.name_len = strlen(s_status.name);
    response.name_is_complete = 1;
    if (!rc) rc = ble_gap_adv_rsp_set_fields(&response);
    struct ble_gap_adv_params params = {.conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN};
    if (!rc) rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    report_error(rc);
}
static void close_window(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.enabled = false; s_status.show_pin = false; s_requested = false;
    xSemaphoreGive(s_lock);
    ble_gap_adv_stop();
    if (s_connection != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(s_connection, BLE_ERR_REM_USER_CONN_TERM);
    clear_input();
}
static void timeout_event(struct ble_npl_event *event) { (void)event; close_window(); }
static void control_event(struct ble_npl_event *event)
{
    (void)event;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool enabled = s_requested;
    unsigned minutes = s_requested_minutes;
    s_status.enabled = enabled;
    s_status.window_minutes = minutes;
    s_status.error = 0;
    xSemaphoreGive(s_lock);
    ble_npl_callout_stop(&s_timeout);
    if (!enabled) close_window();
    else {
        ble_npl_callout_reset(&s_timeout, ble_npl_time_ms_to_ticks32(minutes*60000));
        advertise();
    }
}
esp_err_t hisu_ble_enable(bool enabled)
{
    return hisu_ble_open(enabled ? 5 : 0);
}
esp_err_t hisu_ble_open(unsigned minutes)
{
    if (minutes > 30) return ESP_ERR_INVALID_ARG;
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ready = s_status.ready;
    if (ready) { s_requested = minutes != 0; s_requested_minutes = minutes; }
    xSemaphoreGive(s_lock);
    if (!ready) return ESP_ERR_INVALID_STATE;
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_control);
    return ESP_OK;
}
static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status) { advertise(); break; }
        s_connection = event->connect.conn_handle;
        hisu_ble_status_t current; hisu_ble_get(&current);
        if (!current.enabled) { ble_gap_terminate(s_connection, BLE_ERR_REM_USER_CONN_TERM); break; }
        clear_input();
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.connected = true; s_status.authenticated = false; s_status.show_pin = false;
        s_ap_length = 0;
        xSemaphoreGive(s_lock);
        report_error(ble_gap_security_initiate(s_connection));
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_connection = BLE_HS_CONN_HANDLE_NONE;
        clear_input();
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.connected = false; s_status.authenticated = false; s_status.show_pin = false;
        s_ap_length = 0;
        xSemaphoreGive(s_lock);
        advertise();
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        if (event->passkey.params.action != BLE_SM_IOACT_DISP) return BLE_HS_EINVAL;
        struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP, .passkey = esp_random() % 1000000};
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.pin = io.passkey; s_status.show_pin = true;
        xSemaphoreGive(s_lock);
        report_error(ble_sm_inject_io(event->passkey.conn_handle, &io));
        break;
    }
    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc desc;
        bool authenticated = !event->enc_change.status && !ble_gap_conn_find(event->enc_change.conn_handle, &desc) &&
                             desc.sec_state.encrypted && desc.sec_state.authenticated;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.authenticated = authenticated; s_status.show_pin = false;
        s_status.error = event->enc_change.status;
        xSemaphoreGive(s_lock);
        if (!authenticated) ble_gap_terminate(s_connection, BLE_ERR_AUTH_FAIL);
        break;
    }
    case BLE_GAP_EVENT_ADV_COMPLETE: advertise(); break;
    default: break;
    }
    return 0;
}
static int access_value(uint16_t connection, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)connection; (void)attr;
    hisu_ble_status_t ble; hisu_ble_get(&ble);
    if (!ble.enabled || !ble.authenticated) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    int field = (int)(intptr_t)arg;
    if (field == 7 && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (ctxt->offset == 0) {
            hisu_network_status_t network; hisu_network_get(&network);
            s_ap_value[0] = 1;
            s_ap_value[1] = (network.enabled ? 1 : 0)|(network.scanning ? 2 : 0);
            s_ap_value[2] = network.enabled ? network.count : 0;
            for (int i=0;i<4;++i) s_ap_value[3+i] = (uint32_t)network.error>>(i*8);
            s_ap_length = 7;
            for (int i=0;i<s_ap_value[2];++i) {
                hisu_wifi_ap_t *ap = &network.aps[i];
                unsigned len = strlen(ap->ssid);
                s_ap_value[s_ap_length++] = len;
                memcpy(s_ap_value+s_ap_length,ap->ssid,len); s_ap_length += len;
                s_ap_value[s_ap_length++] = (int8_t)ap->rssi;
                s_ap_value[s_ap_length++] = (ap->secure ? 1 : 0)|(ap->saved ? 2 : 0)|(ap->supported ? 4 : 0);
            }
        }
        return os_mbuf_append(ctxt->om,s_ap_value,s_ap_length) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    }
    if (field == 5 && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        hisu_network_status_t network; hisu_network_get(&network);
        char clock[6]; bool has_time = hisu_clock_read(clock);
        char json[192];
        snprintf(json, sizeof(json), "{\"wifi\":\"%s\",\"error\":%d,\"time\":%s}",
                 network.connected ? "connected" : network.joining ? "joining" : "disconnected",
                 network.error, has_time ? "true" : "false");
        return os_mbuf_append(ctxt->om, json, strlen(json)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    uint16_t size = OS_MBUF_PKTLEN(ctxt->om);
    uint8_t data[65] = {0};
    if (size > 64 || os_mbuf_copydata(ctxt->om, 0, size, data)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (field == 1 && size > 0 && size <= 32 && !memchr(data, 0, size)) {
        clear_input(); memcpy(s_ssid, data, size); err = ESP_OK;
    } else if (field == 2 && !memchr(data, 0, size) && hisu_network_valid_password((char *)data)) {
        memset(s_password, 0, sizeof(s_password)); memcpy(s_password, data, size); err = ESP_OK;
    } else if (field == 3 && size == 1 && data[0] == 1 && s_ssid[0]) {
        err = hisu_network_provision(s_ssid, s_password);
        if (err == ESP_OK) clear_input();
    } else if (field == 4 && size == 10) {
        uint64_t utc = 0;
        for (int i = 0; i < 8; ++i) utc |= (uint64_t)data[i] << (i * 8);
        int16_t offset = (int16_t)((uint16_t)data[8] | (uint16_t)data[9] << 8);
        err = hisu_clock_set(utc, offset);
    } else if (field == 7 && size == 1 && data[0] == 1) {
        err = hisu_network_provision_scan();
    }
    memset(data, 0, sizeof(data));
    return err == ESP_OK ? 0 : err == ESP_ERR_INVALID_ARG ? BLE_ATT_ERR_UNLIKELY : BLE_ATT_ERR_INSUFFICIENT_RES;
}
#define WRITE_FIELD(n) {.uuid=&s_chars[(n)-1].u,.access_cb=access_value,.arg=(void *)(intptr_t)(n),.flags=BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_AUTHEN}
static const struct ble_gatt_svc_def s_services[] = {
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&s_uuid.u,.characteristics=(struct ble_gatt_chr_def[]){
        WRITE_FIELD(1),WRITE_FIELD(2),WRITE_FIELD(3),WRITE_FIELD(4),
        {.uuid=&s_chars[4].u,.access_cb=access_value,.arg=(void *)5,.flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_AUTHEN},
        {.uuid=&s_chars[6].u,.access_cb=access_value,.arg=(void *)7,.flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_AUTHEN|BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_AUTHEN}, {0}}}, {0}
};
static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (!rc) rc = ble_hs_id_infer_auto(0, &s_addr_type);
    xSemaphoreTake(s_lock, portMAX_DELAY); s_status.ready = rc == 0; s_status.error = rc; xSemaphoreGive(s_lock);
}
static void on_reset(int reason)
{
    clear_input(); s_connection = BLE_HS_CONN_HANDLE_NONE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.ready = s_status.enabled = s_status.connected = s_status.authenticated = s_status.show_pin = false;
    s_status.error = reason; s_requested = false;
    s_ap_length = 0;
    xSemaphoreGive(s_lock);
}
static void host_task(void *arg) { (void)arg; nimble_port_run(); nimble_port_freertos_deinit(); }
esp_err_t hisu_ble_start(void)
{
    if (s_lock) return ESP_ERR_INVALID_STATE;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) { vSemaphoreDelete(s_lock); s_lock = NULL; return err; }
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_BT);
    snprintf(s_status.name, sizeof(s_status.name), "HisuOS-%02X%02X", mac[4], mac[5]);
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_mitm = 1; ble_hs_cfg.sm_sc = 1; ble_hs_cfg.sm_bonding = 0;
    ble_svc_gap_init(); ble_svc_gatt_init();
    int rc = ble_svc_gap_device_name_set(s_status.name);
    if (!rc) rc = ble_gatts_count_cfg(s_services);
    if (!rc) rc = ble_gatts_add_svcs(s_services);
    if (rc) { s_status.error = rc; nimble_port_deinit(); vSemaphoreDelete(s_lock); s_lock = NULL; return ESP_FAIL; }
    ble_att_set_preferred_mtu(128);
    ble_npl_event_init(&s_control, control_event, NULL);
    ble_npl_callout_init(&s_timeout, nimble_port_get_dflt_eventq(), timeout_event, NULL);
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
