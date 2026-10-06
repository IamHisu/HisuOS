#include "hisu_setup_portal.h"
#include "hisu_setup_dns.h"
#include "hisu_media_model.h"
#include <stdio.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_ap_get_sta_list.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

/* DNS and redirects share the physically opened, AP-only Setting window. */
static EventGroupHandle_t s_events;
static esp_netif_ip_info_t s_ip;
static int s_socket = -1;
static bool (*s_active)(void);
enum { STOP = 1, DONE = 2 };

bool hisu_setup_socket_ipv4(int fd, bool peer, uint32_t *address)
{
    if (!address) return false;
    struct sockaddr_storage storage = {0};
    socklen_t length = sizeof(storage);
    int err = peer ? getpeername(fd,(struct sockaddr *)&storage,&length) :
                     getsockname(fd,(struct sockaddr *)&storage,&length);
    if (err) return false;
    if (storage.ss_family == AF_INET && length >= sizeof(struct sockaddr_in)) {
        *address = ((const struct sockaddr_in *)&storage)->sin_addr.s_addr;
        return true;
    }
#if CONFIG_LWIP_IPV6
    /* IDF's dual-stack HTTP listener reports IPv4 as ::ffff:a.b.c.d. */
    if (storage.ss_family == AF_INET6 && length >= sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *ipv6 = (const struct sockaddr_in6 *)&storage;
        return hisu_media_ipv4_mapped(ipv6->sin6_addr.s6_addr,address);
    }
#endif
    return false;
}

bool hisu_setup_peer_is_associated(uint32_t address)
{
    wifi_sta_list_t wifi = {0};
    wifi_sta_mac_ip_list_t clients = {0};
    if (!address || esp_wifi_ap_get_sta_list(&wifi) != ESP_OK || wifi.num != 1 ||
        esp_wifi_ap_get_sta_list_with_ip(&wifi, &clients) != ESP_OK) return false;
    return clients.num == 1 && clients.sta[0].ip.addr == address;
}

static bool local_peer(int fd)
{
    uint32_t peer, local;
    if (!hisu_setup_socket_ipv4(fd,true,&peer) ||
        (peer & s_ip.netmask.addr) != (s_ip.ip.addr & s_ip.netmask.addr) ||
        !hisu_setup_peer_is_associated(peer)) return false;
    return hisu_setup_socket_ipv4(fd,false,&local) && local == s_ip.ip.addr;
}

esp_err_t hisu_setup_portal_redirect(httpd_req_t *req)
{
    bool active = s_active && s_active();
    if (!active || !local_peer(httpd_req_to_sockfd(req))) {
        ESP_LOGW("HisuSetting", "Portal denied: %s", active ? "invalid hotspot peer" : "window closed");
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_hdr(req,"Cache-Control","no-store");
        return httpd_resp_sendstr(req, active ? "Connect to the Hisu hotspot to open Setting." : "Setting closed.");
    }
    char url[48];
    snprintf(url, sizeof(url), "http://" IPSTR "/", IP2STR(&s_ip.ip));
    /* HEAD must terminate at the canonical root rather than redirect itself. */
    if (req->method == HTTP_HEAD && !strcmp(req->uri,"/")) {
        char host[64], ip[16];
        snprintf(ip,sizeof(ip),IPSTR,IP2STR(&s_ip.ip));
        if (httpd_req_get_hdr_value_str(req,"Host",host,sizeof(host)) == ESP_OK &&
            hisu_media_http_host_valid(host,ip)) {
            httpd_resp_set_hdr(req,"Cache-Control","no-store");
            return httpd_resp_send(req,NULL,0);
        }
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", url);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    return httpd_resp_sendstr(req, req->method == HTTP_HEAD ? "" : "Hisu Setting");
}

static esp_err_t unknown_post(httpd_req_t *req)
{
    bool allowed = s_active && s_active() && local_peer(httpd_req_to_sockfd(req));
    httpd_resp_set_status(req, allowed ? "404 Not Found" : "403 Forbidden");
    httpd_resp_set_hdr(req, "Connection", "close");
    esp_err_t err = httpd_resp_sendstr(req, "Not found.");
    httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
    return err;
}

esp_err_t hisu_setup_portal_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri = "/*", .method = HTTP_GET, .handler = hisu_setup_portal_redirect},
        {.uri = "/*", .method = HTTP_HEAD, .handler = hisu_setup_portal_redirect},
        {.uri = "/*", .method = HTTP_POST, .handler = unknown_post},
    };
    for (size_t i = 0; i < sizeof(routes)/sizeof(routes[0]); ++i) {
        esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

static void dns_task(void *arg)
{
    (void)arg;
    uint8_t query[512], reply[528];
    while (!(xEventGroupGetBits(s_events) & STOP)) {
        struct sockaddr_in peer;
        socklen_t len = sizeof(peer);
        int received = recvfrom(s_socket, query, sizeof(query), 0, (struct sockaddr *)&peer, &len);
        if (received <= 0 || peer.sin_family != AF_INET || !s_active || !s_active() ||
            (peer.sin_addr.s_addr & s_ip.netmask.addr) != (s_ip.ip.addr & s_ip.netmask.addr) ||
            !hisu_setup_peer_is_associated(peer.sin_addr.s_addr)) continue;
        size_t count = hisu_setup_dns_reply(query, (size_t)received,
            (const uint8_t *)&s_ip.ip.addr, reply, sizeof(reply));
        if (count) sendto(s_socket, reply, count, 0, (struct sockaddr *)&peer, len);
    }
    close(s_socket);
    s_socket = -1;
    xEventGroupSetBits(s_events, DONE);
    vTaskDelete(NULL);
}

void hisu_setup_portal_stop(void)
{
    if (s_events) {
        xEventGroupSetBits(s_events, STOP);
        /* recv timeout bounds shutdown; never delete a task holding a socket. */
        xEventGroupWaitBits(s_events, DONE, pdFALSE, pdTRUE, portMAX_DELAY);
        vEventGroupDelete(s_events); s_events = NULL;
    }
    s_active = NULL;
}

esp_err_t hisu_setup_portal_start(bool (*active)(void))
{
    if (s_events || !active) return ESP_ERR_INVALID_STATE;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!netif || esp_netif_get_ip_info(netif, &s_ip) != ESP_OK || !s_ip.ip.addr) return ESP_ERR_INVALID_STATE;
    s_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_socket < 0) return ESP_FAIL;
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(53),
                                  .sin_addr.s_addr = s_ip.ip.addr};
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 250000};
    if (setsockopt(s_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
        setsockopt(s_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) ||
        bind(s_socket, (struct sockaddr *)&address, sizeof(address))) {
        close(s_socket); s_socket = -1; return ESP_FAIL;
    }
    s_events = xEventGroupCreate();
    s_active = active;
    if (!s_events || xTaskCreate(dns_task, "hisu_dns", 4096, NULL, 3, NULL) != pdPASS) {
        if (s_events) vEventGroupDelete(s_events);
        s_events = NULL; s_active = NULL; close(s_socket); s_socket = -1;
        return ESP_ERR_NO_MEM;
    }
    esp_netif_dns_info_t dns = {.ip.type = ESP_IPADDR_TYPE_V4};
    dns.ip.u_addr.ip4.addr = s_ip.ip.addr;
    esp_err_t err = esp_netif_dhcps_stop(netif);
    if (err == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) err = ESP_OK;
    uint8_t offer = 0x02; /* DHCPS_OFFER_DNS, as in IDF softap_sta example. */
    if (err == ESP_OK) err = esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET,
        ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
    if (err == ESP_OK) err = esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
    esp_err_t restart = esp_netif_dhcps_start(netif);
    if (err == ESP_OK) err = restart;
    if (err != ESP_OK) { hisu_setup_portal_stop(); return err; }
    return err;
}
