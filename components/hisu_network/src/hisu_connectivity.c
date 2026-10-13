#include "hisu_connectivity.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <string.h>

static hisu_internet_state_t probe(const char *url, const char *expected)
{
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 4000,
        .disable_auto_redirect = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 256,
        .buffer_size_tx = 256,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return HISU_INTERNET_OFFLINE;
    hisu_internet_state_t result = HISU_INTERNET_OFFLINE;
    if (esp_http_client_open(client, 0) == ESP_OK && esp_http_client_fetch_headers(client) >= 0) {
        int status = esp_http_client_get_status_code(client);
        if ((status >= 300 && status < 400) || status == 511) result = HISU_INTERNET_CAPTIVE;
        else if (!expected && status == 204) result = HISU_INTERNET_ONLINE;
        else if (status == 200) {
            /* Read one byte beyond the expected body, never an entire login page. */
            char body[23];
            int length = 0, n = 0;
            while (length < (int)sizeof(body)) {
                n = esp_http_client_read(client, body + length, sizeof(body) - length);
                if (n <= 0) break;
                length += n;
            }
            if (n >= 0 && (length == (int)sizeof(body) || esp_http_client_is_complete_data_received(client))) {
                result = expected && length == (int)strlen(expected) &&
                    esp_http_client_is_complete_data_received(client) && !memcmp(body, expected, length)
                    ? HISU_INTERNET_ONLINE : HISU_INTERNET_CAPTIVE;
            }
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}

hisu_internet_state_t hisu_connectivity_probe(void)
{
    hisu_internet_state_t primary = probe("https://connectivitycheck.gstatic.com/generate_204", NULL);
    if (primary == HISU_INTERNET_ONLINE) return primary;
    /* Independent HTTP probe detects redirects even when a portal blocks TLS,
     * and avoids a false offline result while the clock is still synchronizing. */
    hisu_internet_state_t secondary = probe("http://www.msftconnecttest.com/connecttest.txt", "Microsoft Connect Test");
    if (secondary == HISU_INTERNET_ONLINE) return secondary;
    return primary == HISU_INTERNET_CAPTIVE || secondary == HISU_INTERNET_CAPTIVE
        ? HISU_INTERNET_CAPTIVE : HISU_INTERNET_OFFLINE;
}
